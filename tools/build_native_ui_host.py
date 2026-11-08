"""Build original native composition hosts, each importing one game library.

Only linkage names are copied from the metadata catalog. Game movie bytes,
artwork, fonts and ActionScript implementations remain in the game's PAK.
The bridge emits AVM1 NewObject/CallMethod directly to support mixed native
object/scalar arguments and object results without a C++ GFxValue ABI clone.
"""

import argparse
import json
import re
import struct
import zipfile
from pathlib import Path

from native_ui_bytecode import action, function, push, root_member, tag, when
from inspect_ui_assets import inspect_archive

FRAME_RATE = 30


def value(name):
    return push(name) + action(0x1C)


def bridge(name, construct):
    # Preserve output/member beneath the dynamic argument stack. AVM1 pops
    # argument zero first, so push array elements from last to first.
    body = value("output") + push("value")
    count = value("items") + push("length") + action(0x4E)
    body += push("index") + count + push(1) + action(0x0B) + action(0x3C)
    condition = value("index") + push(0) + action(0x48)
    iteration = value("items") + value("index") + action(0x4E)
    iteration += push("index") + value("index") + push(1) + action(0x0B) + action(0x1D)
    # If index < 0, skip the iteration and its backward Jump record.
    loop_size = len(condition) + 5 + len(iteration) + 5
    body += condition + action(0x9D, struct.pack("<h", len(iteration) + 5))
    body += iteration + action(0x99, struct.pack("<h", -loop_size))
    body += count
    if construct:
        body += value("className") + action(0x40)
        parameters = ["className", "items", "output"]
    else:
        body += value("receiver") + value("method") + action(0x52)
        parameters = ["receiver", "method", "items", "output"]
    body += action(0x4F) + push(True) + action(0x3E) + action(0)
    return function(name, parameters, body)


def control_route(name, handler, parameters):
    # Native menu timelines invoke _root services with the originating clip.
    # Walk its real display ancestry to the nearest page boundary. The page
    # handler receives the original arguments, with the page as its receiver.
    setup = push("owner") + value(parameters[0]) + action(0x3C)
    condition = value("owner") + action(0x12)
    call = b"".join(value(parameter) for parameter in reversed(parameters))
    call += push(len(parameters)) + value("owner") + push(handler) + action(0x52) + action(0x17)
    call += push(True) + action(0x3E)
    iteration = when(value("owner") + push("kcdcNativePage") + action(0x4E), call)
    iteration += push("owner") + value("owner") + push("_parent") + action(0x4E) + action(0x1D)
    loop_size = len(condition) + 5 + len(iteration) + 5
    body = setup + condition + action(0x9D, struct.pack("<h", len(iteration) + 5))
    body += iteration + action(0x99, struct.pack("<h", -loop_size))
    body += push(False) + action(0x3E) + action(0)
    return function(name, parameters, body)


def slider_cleanup(dispose):
    # These properties/handlers belong to the native Slider implementation
    # shared by ApseModalDialog, ItemSelection and ItemTransfer. Cancellation
    # keeps its persistent interaction closures for the next focus acquisition.
    def member(receiver, name):
        return value(receiver) + push(name) + action(0x4E)

    def call(receiver, name, arguments=()):
        return b"".join(reversed(arguments)) + push(len(arguments)) + value(receiver) + push(name) + action(0x52) + action(0x17)

    def remove(receiver, name):
        return value(receiver) + push(name) + action(0x3A) + action(0x17)

    body = push("clip") + member("slider", "m_SliderMC") + action(0x3C)
    body += push("thumb") + member("clip", "thumb") + action(0x3C)
    body += push("mask") + member("clip", "mask") + action(0x3C)
    body += push("wasDragging") + member("slider", "m_Dragging") + action(0x3C)
    body += value("slider") + push("m_Dragging", False) + action(0x4F)
    thumb = when(value("wasDragging"), call("thumb", "stopDrag")) + call("thumb", "tweenEnd", [push(False)])
    thumb += remove("thumb", "onRelease") + remove("thumb", "onReleaseOutside")
    if dispose:
        for name in ("onPress", "onRollOver", "onRollOut"):
            thumb += remove("thumb", name)
    body += when(value("thumb"), thumb)
    body += when(value("mask"), call("mask", "tweenEnd", [push(False)]))
    body += when(value("clip"), remove("clip", "onEnterFrame"))
    if dispose:
        body += push("trackButton") + member("clip", "trackButton") + action(0x3C)
        body += when(value("trackButton"), remove("trackButton", "onRelease"))
    else:
        body += call("slider", "SetPosition", [member("slider", "m_Curr"), push(False)])
    body = when(value("slider"), body) + action(0)
    return function("KCDC_DisposeSlider" if dispose else "KCDC_CancelSlider", ["slider"], body)


def periodic_updates():
    # Native HUD drives NotificationManager through setInterval at this rate.
    # Return an ordinary token so page cleanup can retain and stop the timer.
    start = push("token", 0) + action(0x43) + action(0x3C)
    start += value("token") + push("intervalId", 1000) + value("_root.g_Framerate") + action(0x0D)
    start += value("method") + value("receiver") + push(3, "setInterval") + action(0x3D) + action(0x4F)
    started = value("token") + push("intervalId") + action(0x4E) + action(0x44) + push("number") + action(0x49)
    start += when(started, value("token") + action(0x3E))
    callable_method = value("receiver") + value("method") + action(0x4E) + action(0x44) + push("function") + action(0x49)
    start = when(value("receiver"), when(callable_method, start)) + action(0)
    stop = push("id") + value("token") + push("intervalId") + action(0x4E) + action(0x3C)
    stop += value("token") + push("intervalId") + action(0x3A) + action(0x17)
    # Native clearInterval rejects undefined/NaN, making repeated stop harmless.
    stop += value("id") + push(1, "clearInterval") + action(0x3D) + action(0x17)
    stop = when(value("token"), stop) + action(0)
    return function("KCDC_StartPeriodic", ["receiver", "method"], start) + function("KCDC_StopPeriodic", ["token"], stop)


def notification_cleanup():
    # RemoveAndFlush only schedules removal. A stopped manager needs its
    # proven removeMC path now, including subclass ClearBeforeRemove hooks.
    def member(name):
        return value("manager") + push(name) + action(0x4E)

    body = value("manager") + push("kcdcDisposed", True) + action(0x4F)
    body += value("manager") + push("IsFlushing", True) + action(0x4F)
    body += push(0) + value("manager") + push("Pause") + action(0x52) + action(0x17)
    body += value("manager") + push("m_QueueA", 0) + action(0x42) + action(0x4F)
    count = member("m_NotificationsA") + push("length") + action(0x4E)
    condition = count + push(0) + action(0x67) + action(0x12)
    iteration = count + push(1) + action(0x0B) + push(1) + value("manager") + push("removeMC") + action(0x52) + action(0x17)
    loop_size = len(condition) + 5 + len(iteration) + 5
    body += condition + action(0x9D, struct.pack("<h", len(iteration) + 5))
    body += iteration + action(0x99, struct.pack("<h", -loop_size))
    body = when(value("manager"), when(member("kcdcDisposed") + action(0x12), body)) + action(0)
    return function("KCDC_DisposeNotifications", ["manager"], body)


def general_notification_manager():
    # Compose a native base manager with a page-local factory override. No
    # mutation of NotificationManager.prototype or native HUD singleton state.
    factory = value("dataA") + value("duration") + value("priority") + value("id") + push(4, "GeneralNotification") + action(0x40) + action(0x3E) + action(0)
    body = push("manager") + value("container") + push(1, "NotificationManager") + action(0x40) + action(0x3C)
    body += value("manager") + push("CreateNotification") + function("", ["id", "priority", "duration", "dataA"], factory) + action(0x4F)
    body += value("manager") + action(0x3E) + action(0)
    return function("KCDC_CreateGeneralNotificationManager", ["container"], body)


def crime_list_item_update():
    # CrimeListItem.Set appends nested items; page updates replace their data.
    # Reset before Set so its DataChanged notification observes the new list.
    body = value("item") + push("StolenItemsA", 0) + action(0x42) + action(0x4F)
    body += value("data") + push(1) + value("item") + push("Set") + action(0x52) + action(0x17)
    body += action(0)
    return function("KCDC_SetCrimeListItem", ["item", "data"], body)


def mask_binding():
    # Sprite-only masks; never let the native role/backpointer setter receive
    # the same character as both owner and mask. Null explicitly clears.
    target_sprite = value("target") + value("MovieClip") + action(0x54)
    mask_sprite = value("mask") + value("MovieClip") + action(0x54)
    mask_null = value("mask") + action(0x96, b"\x02") + action(0x66)
    distinct = value("target") + value("mask") + action(0x66) + action(0x12)
    call = value("mask") + push(1) + value("target") + push("setMask") + action(0x52) + action(0x17)
    call += push(True) + action(0x3E)
    body = when(target_sprite + mask_sprite + mask_null + action(0x11) + action(0x10) + distinct + action(0x10), call)
    body += push(False) + action(0x3E) + action(0)
    return function("KCDC_SetMask", ["target", "mask"], body)


def hit_area_binding():
    # Native hitArea keeps a handle plus a single owner backpointer. Use one
    # area per owner; an explicit null releases the native registration.
    target_sprite = value("target") + value("MovieClip") + action(0x54)
    area_sprite = value("area") + value("MovieClip") + action(0x54)
    area_null = value("area") + action(0x96, b"\x02") + action(0x66)
    distinct = value("target") + value("area") + action(0x66) + action(0x12)
    assign = value("target") + push("hitArea") + value("area") + action(0x4F)
    assign += push(True) + action(0x3E)
    body = when(target_sprite + area_sprite + area_null + action(0x11) + action(0x10) + distinct + action(0x10), assign)
    body += push(False) + action(0x3E) + action(0)
    return function("KCDC_SetHitArea", ["target", "area"], body)


def movie(library, exports, element_name="KCDCNativeHost"):
    bits = f"{17:05b}" + "".join(f"{number:017b}" for number in (0, 38400, 0, 21600))
    bits += "0" * (-len(bits) % 8)
    body = int(bits, 2).to_bytes(len(bits) // 8, "big") + struct.pack("<HH", FRAME_RATE * 256, 1)
    source = element_name.encode("ascii")
    body += tag(1000, struct.pack("<HIH", 0x300, 0, 1) + b"\0" + bytes([len(source)]) + source)
    body += tag(69, struct.pack("<I", 0))
    imports = ("/" + library).encode("ascii") + b"\0" + struct.pack("<H", len(exports))
    for identifier, name in enumerate(exports, 1):
        imports += struct.pack("<H", identifier) + name.encode("ascii") + b"\0"
    body += tag(57, imports)
    code = bridge("KCDC_Construct", True) + bridge("KCDC_Invoke", False)
    # Native Notification converts milliseconds to frame counts through its
    # captured root. Match the 30 fps movie header instead of leaving NaN.
    code += root_member("g_Framerate", push(FRAME_RATE))
    code += slider_cleanup(False) + slider_cleanup(True)
    code += periodic_updates()
    code += notification_cleanup()
    code += general_notification_manager()
    code += mask_binding()
    code += hit_area_binding()
    code += crime_list_item_update()
    for name, handler, parameters in [
        ("OnButton", "onNativeButton", ["button", "secondAction"]),
        ("selectBtn", "onNativeSelect", ["button"]),
        ("scrollbarUpdateByButton", "onNativeScroll", ["scrollbar"]),
        ("arettationScrollbar", "onNativeScrollEnd", ["scrollbar"]),
    ]:
        code += control_route(name, handler, parameters)
    code += root_member("kcdcNativeControlRoutingVersion", push(1))
    for function_name, member_name, parameters in [("fc_setInput", "onInput", ["actionName", "activationMode"]), ("KCDC_CancelInteraction", "onCancel", [])]:
        dispatch = push("target") + value("_root.kcdcFocusedPage") + action(0x3C)
        call = b"".join(value(parameter) for parameter in reversed(parameters))
        call += push(len(parameters)) + value("target") + push(member_name) + action(0x52) + action(0x17)
        if member_name == "onInput":
            call = root_member("kcdcInputCancelled", push(False)) + call
        dispatch += when(value("target"), call)
        if member_name == "onCancel":
            dispatch = root_member("kcdcCancellingInput", push(True)) + root_member("kcdcInputCancelled", push(True)) + dispatch + root_member("kcdcCancellingInput", push(False))
            allow = value("_root.kcdcCancellingInput") + action(0x12) + value("_root.kcdcInputCancelled") + action(0x12) + action(0x10)
            dispatch = when(allow, dispatch)
        dispatch += action(0)
        code += function(function_name, parameters, dispatch)
    select = push(0) + value("_root") + push("KCDC_CancelInteraction") + action(0x52) + action(0x17)
    select += root_member("kcdcFocusedPage", value("page")) + root_member("kcdcInputCancelled", push(False)) + action(0)
    code += function("KCDC_SetInputPage", ["page"], select)
    code += root_member("kcdcNativeUIReady", push(False))
    code += root_member("kcdcNativeCompositionVersion", push(1))
    # Native FlashUI has not yet cached its root during cry_onSetup. Report
    # readiness from the first subsequent root update, then remove our handler.
    ready = root_member("kcdcNativeUIReady", push(True))
    ready += value("_root") + push("onEnterFrame") + action(0x3A) + action(0x17)
    ready += action(0x83, b"FSCommand:NativeUIReady\0host-v1\0")
    ready += action(0)
    setup = root_member("onEnterFrame", function("", [], ready)) + action(0)
    code += function("cry_onSetup", [], setup)
    code += action(0x07) + action(0)
    body += tag(12, code) + tag(1) + tag(0)
    return b"GFX\x08" + struct.pack("<I", len(body) + 8) + body


DESCRIPTOR = """<?xml version="1.0" encoding="utf-8"?>
<UIElements name="KCDC">
  <UIElement name="KCDCNativeHost" cursor="0" mouseevents="0"
             keyevents="0" controller_input="0" events_exclusive="0">
    <GFx file="KCDCNativeHost.gfx" layer="46" alpha="0">
      <Constraints><Align mode="fullscreen" scale="1"/></Constraints>
    </GFx>
    <events>
      <event name="NativeUIReady" fscommand="NativeUIReady"/>
      <event name="OnPlayAudio" fscommand="onPlayAudio"><param name="TriggerName" type="string"/></event>
      <event name="OnStopAudio" fscommand="onStopAudio"><param name="TriggerName" type="string"/></event>
      <event name="OnSetVolumeAudio" fscommand="onSetVolumeAudio"><param name="TriggerName" type="string"/><param name="Volume" type="float"/></event>
      <event name="GetButtonId" fscommand="getButtonId"><param name="ActionName" type="string"/></event>
    </events>
  </UIElement>
</UIElements>
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=Path("tools/data/native_ui/ui-component-catalog.json"))
    parser.add_argument("--library", default="Libs/UI/AlchemyBook.gfx")
    parser.add_argument("--all-libraries", action="store_true", help="Bundle separate hosts for every library with sprite exports; each host imports only its own library")
    parser.add_argument("--output", type=Path, default=Path("files/kcdc/native_ui"))
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    source = next(item for item in catalog["movies"] if item["file"] == args.library)
    sources = [source] + [item for item in catalog["movies"] if item is not source] if args.all_libraries else [source]
    entries = {}
    libraries = []
    skipped = []
    for source in sources:
        exports = [item["name"] for item in source["exports"] if item["definition"] and item["definition"]["kind"] == "sprite"]
        if not exports and args.all_libraries and source["file"] != args.library:
            skipped.append(source["file"])
            continue
        if not exports or len(exports) > 65535 or len(set(exports)) != len(exports):
            raise ValueError("Library must expose a nonempty, unique set of sprite linkage names")
        element_name = "KCDCNativeHost" if source["file"] == args.library else "KCDCNativeHost_" + Path(source["file"]).stem
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", element_name) or len(element_name) > 255:
            raise ValueError(f"Library cannot form a native host identifier: {source['file']}")
        movie_path = f"Libs/UI/{element_name}.gfx"
        if movie_path in entries:
            raise ValueError(f"Duplicate native host name: {element_name}")
        entries[movie_path] = movie(source["file"], exports, element_name)
        entries[f"Libs/UI/UIElements/{element_name}.xml"] = DESCRIPTOR.replace("KCDCNativeHost", element_name).encode("utf-8")
        libraries.append({"host": element_name, "movie": source["file"], "sha256": source["sha256"], "exports": exports})
    args.output.mkdir(parents=True, exist_ok=True)
    archive_path = args.output / "native-ui-host.pak"
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, data in entries.items():
            archive.writestr(zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0)), data)
    report = inspect_archive(archive_path)
    report["native_library"] = libraries[0]
    report["native_libraries"] = libraries
    report["libraries_without_sprite_exports"] = skipped
    (args.output / "inventory.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Built {archive_path}: {len(libraries)} separate original hosts, {sum(len(item['exports']) for item in libraries)} native sprite/class imports")


if __name__ == "__main__":
    main()
