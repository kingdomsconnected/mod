#include "features/animation/animation_scripting.h"

#include "core/server.h"

#include "shared/streamed_assets/streamed_asset_policy.h"
#include "shared/game/animation_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/option_field.h"
#include "shared/scripting/rotation_arg.h"

#include <scripting/v8pp_types.h>
#include <utils/string_utils.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        // The two hands' joints on both human skeletons, the ones the game's
        // own `hand_right` and `hand_left` sockets hang on.
        constexpr const char *kRightHandJoint = "righthandslot";
        constexpr const char *kLeftHandJoint  = "lefthandslot";

        /**
         * Whether the game treats this fragment as full-body, which is what
         * decides whether the player's own body has to be held still under
         * it. The exact tag combination answers first; a combination the
         * table does not list falls back to what any row of the fragment says.
         */
        bool IsFullBody(const std::string &fragment, const std::string &tags) {
            bool anyFullBody = false;
            for (const Shared::Game::AnimationEntry &entry : Shared::Game::g_Animations) {
                if (!Framework::Utils::StringUtils::EqualsIgnoreCase(entry.fragment, fragment)) {
                    continue;
                }
                if (Framework::Utils::StringUtils::EqualsIgnoreCase(entry.tags, tags)) {
                    return entry.fullBody;
                }
                anyFullBody = anyFullBody || entry.fullBody;
            }
            return anyFullBody;
        }

        bool Throw(v8::Isolate *isolate, const char *caller, const std::string &message) {
            isolate->ThrowError(v8pp::to_v8(isolate, std::string(caller) + ": " + message));
            return false;
        }

        bool ParseProp(v8::Isolate *isolate, v8::Local<v8::Value> value, const char *caller, Shared::Game::AnimationProp &out) {
            if (!value->IsObject()) {
                return Throw(isolate, caller, "a prop is an object carrying `item` or `model`");
            }
            const v8::Local<v8::Object> spec = value.As<v8::Object>();

            const v8::Local<v8::Value> item  = OptionField(isolate, spec, "item");
            const v8::Local<v8::Value> model = OptionField(isolate, spec, "model");
            glm::vec3 position(0.0F);
            glm::quat rotation = glm::identity<glm::quat>();
            if (!item.IsEmpty() && item->IsString()) {
                const std::string name                       = v8pp::from_v8<std::string>(isolate, item);
                const Shared::Game::AnimationPropEntry *entry = Shared::Game::FindAnimationProp(name);
                if (entry == nullptr) {
                    return Throw(isolate, caller, "'" + name + "' is not a prop; see Animations.props()");
                }
                out.model = entry->model;
                position  = glm::vec3(entry->position[0], entry->position[1], entry->position[2]);
                rotation  = glm::quat(glm::vec3(entry->rotation[0], entry->rotation[1], entry->rotation[2]));
            }
            else if (!model.IsEmpty() && model->IsString()) {
                out.model = Framework::Utils::StringUtils::ToLower(v8pp::from_v8<std::string>(isolate, model));
                if (!Shared::Game::AnimationProp::IsModelPath(out.model) || out.model.size() > Shared::Game::AnimationProp::kMaxModelLength) {
                    return Throw(isolate, caller, "'" + out.model + "' is not a model path; a prop model is an `objects/...cgf` path");
                }
            }
            else {
                return Throw(isolate, caller, "a prop needs `item` (a name from Animations.props()) or `model` (an objects/...cgf path)");
            }

            const v8::Local<v8::Value> joint = OptionField(isolate, spec, "joint");
            const v8::Local<v8::Value> hand  = OptionField(isolate, spec, "hand");
            if (!joint.IsEmpty() && joint->IsString()) {
                out.joint = v8pp::from_v8<std::string>(isolate, joint);
            }
            else if (!hand.IsEmpty() && hand->IsString()) {
                const std::string side = v8pp::from_v8<std::string>(isolate, hand);
                if (side != "right" && side != "left") {
                    return Throw(isolate, caller, "a prop's `hand` is \"right\" or \"left\"");
                }
                out.joint = side == "left" ? kLeftHandJoint : kRightHandJoint;
            }
            else {
                out.joint = kRightHandJoint;
            }

            const v8::Local<v8::Value> offset = OptionField(isolate, spec, "position");
            if (!offset.IsEmpty() && offset->IsObject()) {
                position = v8pp::convert<glm::vec3>::from_v8(isolate, offset);
            }
            out.position = position;
            out.rotation = ParseRotationArg(isolate, OptionField(isolate, spec, "rotation"), rotation);
            if (!out.IsSane()) {
                return Throw(isolate, caller, "a prop's joint must be named, and its offset has to stay within 4 metres of it");
            }
            return true;
        }

        v8::Local<v8::Object> AnimationInfoToV8(v8::Isolate *isolate, const Shared::Game::AnimationEntry &entry) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> out      = v8::Object::New(isolate);
            out->Set(context, v8pp::to_v8(isolate, "fragment"), v8pp::to_v8(isolate, entry.fragment)).Check();
            out->Set(context, v8pp::to_v8(isolate, "tags"), v8pp::to_v8(isolate, entry.tags)).Check();
            out->Set(context, v8pp::to_v8(isolate, "duration"), v8pp::to_v8(isolate, entry.duration)).Check();
            out->Set(context, v8pp::to_v8(isolate, "oneShot"), v8pp::to_v8(isolate, entry.oneShot)).Check();
            out->Set(context, v8pp::to_v8(isolate, "fullBody"), v8pp::to_v8(isolate, entry.fullBody)).Check();
            out->Set(context, v8pp::to_v8(isolate, "rightHand"), v8pp::to_v8(isolate, entry.rightHand)).Check();
            out->Set(context, v8pp::to_v8(isolate, "leftHand"), v8pp::to_v8(isolate, entry.leftHand)).Check();
            out->Set(context, v8pp::to_v8(isolate, "male"), v8pp::to_v8(isolate, (entry.bodies & Shared::Game::kAnimationBodyMale) != 0)).Check();
            out->Set(context, v8pp::to_v8(isolate, "female"), v8pp::to_v8(isolate, (entry.bodies & Shared::Game::kAnimationBodyFemale) != 0)).Check();
            out->Set(context, v8pp::to_v8(isolate, "sequence"), v8pp::to_v8(isolate, entry.sequence)).Check();
            out->Set(context, v8pp::to_v8(isolate, "phase"), v8pp::to_v8(isolate, entry.phase)).Check();
            out->Set(context, v8pp::to_v8(isolate, "partner"), v8pp::to_v8(isolate, entry.partner)).Check();
            out->Set(context, v8pp::to_v8(isolate, "slaveBody"), v8pp::to_v8(isolate, entry.slaveBody)).Check();
            return out;
        }

        v8::Local<v8::Array> PropInfos(v8::Isolate *isolate) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> out       = v8::Array::New(isolate, static_cast<int>(Shared::Game::g_AnimationProps.size()));
            std::uint32_t index            = 0;
            for (const Shared::Game::AnimationPropEntry &entry : Shared::Game::g_AnimationProps) {
                v8::Local<v8::Object> prop = v8::Object::New(isolate);
                prop->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, entry.name)).Check();
                prop->Set(context, v8pp::to_v8(isolate, "model"), v8pp::to_v8(isolate, entry.model)).Check();
                prop->Set(context, v8pp::to_v8(isolate, "hand"), v8pp::to_v8(isolate, entry.manipulation)).Check();
                out->Set(context, index++, prop).Check();
            }
            return out;
        }
    } // namespace

    bool Animations::ParseRequest(const v8::FunctionCallbackInfo<v8::Value> &info, int first, const char *caller, Shared::Game::AnimationPlayback &out) {
        v8::Isolate *isolate = info.GetIsolate();
        if (info.Length() <= first || !info[first]->IsString()) {
            return Throw(isolate, caller, "expected (fragment, options?); the fragment is a name from Animations.list, or \"\" to only hold a prop");
        }
        out          = {};
        out.fragment = v8pp::from_v8<std::string>(isolate, info[first]);
        if (out.fragment.size() > Shared::Game::AnimationPlayback::kMaxFragmentLength) {
            return Throw(isolate, caller, "that fragment name is longer than any the game has");
        }

        bool lockMovementGiven = false;
        if (info.Length() > first + 1 && info[first + 1]->IsObject()) {
            const v8::Local<v8::Object> options = info[first + 1].As<v8::Object>();

            const v8::Local<v8::Value> tags = OptionField(isolate, options, "tags");
            if (!tags.IsEmpty() && tags->IsString()) {
                out.tags = v8pp::from_v8<std::string>(isolate, tags);
                if (out.tags.size() > Shared::Game::AnimationPlayback::kMaxTagsLength) {
                    return Throw(isolate, caller, "`tags` is longer than any combination the game uses");
                }
            }
            const v8::Local<v8::Value> clip = OptionField(isolate, options, "clip");
            if (!clip.IsEmpty() && clip->IsString()) {
                std::string path;
                for (const char c : v8pp::from_v8<std::string>(isolate, clip)) {
                    path.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
                if (path.size() < 4 || path.compare(path.size() - 4, 4, ".caf") != 0) {
                    path += ".caf";
                }
                if (!KCDC::Shared::StreamedAssets::Policy::IsStreamedClipPath(path) || !Server::Get()->GetAssetPaks().HasEntry(KCDC::Shared::StreamedAssets::kLaneStream, path)) {
                    return Throw(isolate, caller, "`clip` names no clip this server streams; it is a path like animations/kcdc/<resource>/wave.caf in a resource's stream folder");
                }
                if (!out.fragment.empty() && out.fragment != KCDC::Shared::StreamedAssets::kStreamedClipFragment) {
                    return Throw(isolate, caller, "a clip plays through its own fragment; pass \"\" as the fragment");
                }
                out.fragment = KCDC::Shared::StreamedAssets::kStreamedClipFragment;
                out.clip     = path;
            }

            const v8::Local<v8::Value> loop = OptionField(isolate, options, "loop");
            out.loop                        = !loop.IsEmpty() && loop->IsBoolean() && loop->BooleanValue(isolate);

            const v8::Local<v8::Value> lockMovement = OptionField(isolate, options, "lockMovement");
            if (!lockMovement.IsEmpty() && lockMovement->IsBoolean()) {
                lockMovementGiven = true;
                out.lockMovement  = lockMovement->BooleanValue(isolate);
            }

            const v8::Local<v8::Value> props = OptionField(isolate, options, "props");
            if (!props.IsEmpty() && props->IsArray()) {
                const v8::Local<v8::Array> list = props.As<v8::Array>();
                if (list->Length() > Shared::Game::AnimationPlayback::kMaxProps) {
                    return Throw(isolate, caller, "a body holds at most two props");
                }
                for (std::uint32_t i = 0; i < list->Length(); ++i) {
                    v8::Local<v8::Value> element;
                    if (!list->Get(isolate->GetCurrentContext(), i).ToLocal(&element) || !ParseProp(isolate, element, caller, out.props[i])) {
                        return false;
                    }
                }
            }
            else if (!props.IsEmpty() && props->IsObject() && !ParseProp(isolate, props, caller, out.props[0])) {
                return false;
            }
        }
        if (!lockMovementGiven) {
            // A clip's fragment takes the whole body and lets the clip drive its root.
            out.lockMovement = !out.clip.empty() || (!out.fragment.empty() && IsFullBody(out.fragment, out.tags));
        }
        if (out.fragment.empty() && out.tags.empty() && !out.props[0].IsSet() && !out.props[1].IsSet()) {
            return Throw(isolate, caller, "nothing to play: give a fragment, tags or a prop");
        }
        return true;
    }

    bool Animations::ParseStandUp(const v8::FunctionCallbackInfo<v8::Value> &info, int first) {
        if (info.Length() <= first || !info[first]->IsObject()) {
            return false;
        }
        const v8::Local<v8::Value> standUp = OptionField(info.GetIsolate(), info[first].As<v8::Object>(), "standUp");
        return !standUp.IsEmpty() && standUp->IsBoolean() && standUp->BooleanValue(info.GetIsolate());
    }

    bool Animations::ParseExpression(const v8::FunctionCallbackInfo<v8::Value> &info, int first, const char *caller, std::string &fragment, std::string &tags) {
        v8::Isolate *isolate = info.GetIsolate();
        if (info.Length() <= first || !info[first]->IsString()) {
            return Throw(isolate, caller, "expected (fragment, options?); the fragment is a facial one like FE_DialogueSpeaking or ADLG_FA_Smile, or \"\" for the game's own face");
        }
        fragment = v8pp::from_v8<std::string>(isolate, info[first]);
        tags     = StringOption(isolate, info, first + 1, "tags");
        if (fragment.size() > Shared::Game::FaceState::kMaxFragmentLength) {
            return Throw(isolate, caller, "that fragment name is longer than any the game has");
        }
        if (tags.size() > Shared::Game::FaceState::kMaxTagsLength) {
            return Throw(isolate, caller, "`tags` is longer than any combination the game uses");
        }
        if (fragment.empty() && !tags.empty()) {
            return Throw(isolate, caller, "tags pick a variant of a fragment; give the fragment too");
        }
        return true;
    }

    bool Animations::ParseTalking(const v8::FunctionCallbackInfo<v8::Value> &info, int first, const char *caller, Shared::Game::TalkStyle &style) {
        v8::Isolate *isolate = info.GetIsolate();
        if (info.Length() <= first || !info[first]->IsBoolean()) {
            return Throw(isolate, caller, "expected (talking, style?)");
        }
        if (!info[first]->BooleanValue(isolate)) {
            style = Shared::Game::TalkStyle::None;
            return true;
        }
        style = Shared::Game::TalkStyle::Neutral;
        if (info.Length() <= first + 1 || info[first + 1]->IsUndefined()) {
            return true;
        }
        // `none` is `setTalking(false)`, never a style to talk in.
        style = info[first + 1]->IsString() ? Shared::Game::TalkStyleFromName(v8pp::from_v8<std::string>(isolate, info[first + 1])) : Shared::Game::TalkStyle::Count;
        if (style != Shared::Game::TalkStyle::None && style != Shared::Game::TalkStyle::Count) {
            return true;
        }
        return Throw(isolate, caller, "the style is \"neutral\", \"happy\", \"drunk\" or \"chew\"");
    }

    // JS: Animations.props() -> AnimationPropInfo[]
    void Animations::JS_Props(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::HandleScope handleScope(info.GetIsolate());
        info.GetReturnValue().Set(PropInfos(info.GetIsolate()));
    }

    // JS: Animations.list(search?) -> AnimationInfo[]
    void Animations::JS_List(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        const std::string search = info.Length() >= 1 && info[0]->IsString() ? v8pp::from_v8<std::string>(isolate, info[0]) : std::string();

        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> out       = v8::Array::New(isolate);
        std::uint32_t index            = 0;
        for (const Shared::Game::AnimationEntry &entry : Shared::Game::g_Animations) {
            if (!Framework::Utils::StringUtils::ContainsIgnoreCase(entry.fragment, search) && !Framework::Utils::StringUtils::ContainsIgnoreCase(entry.tags, search)) {
                continue;
            }
            out->Set(context, index++, AnimationInfoToV8(isolate, entry)).Check();
        }
        info.GetReturnValue().Set(out);
    }

    void Animations::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &animationInfo = ServerCatalog().data_type("AnimationInfo", "One animation the game plays on a human body: a Mannequin fragment and the tags that pick its variant, as a row of the game's own `actor_anim_action` table.");
        animationInfo.add_property("fragment", "string", "The fragment's name, which is what `playAnimation` takes.");
        animationInfo.add_property("tags", "string", "The tags that select this variant, `+`-separated, which is what `playAnimation`'s `tags` option takes. Empty for the fragment's default.");
        animationInfo.add_property("duration", "number", "How long one pass lasts, in seconds.");
        animationInfo.add_property("oneShot", "boolean", "Whether it ends by itself. One that does not is a loop and plays until something replaces it, `loop` or not.");
        animationInfo.add_property("fullBody", "boolean", "Whether it takes the whole body. A full-body animation stops the legs, so a player playing one is held still unless `lockMovement` says otherwise.");
        animationInfo.add_property("rightHand", "string", "What the right hand is animated holding, as the game names the kind (`bucket`, `axe_tool`), or empty for nothing. Match it against `AnimationPropInfo.hand` to find a prop that fits.");
        animationInfo.add_property("leftHand", "string", "The same for the left hand.");
        animationInfo.add_property("male", "boolean", "Whether the game has it for a man's body.");
        animationInfo.add_property("female", "boolean", "Whether the game has it for a woman's body.");
        animationInfo.add_property("sequence", "string", "The in/loop/out sequence this fragment is a part of, shared by every fragment in it (`BakerMill` for `BakerMillIn`, `BakerMillLoop` and `BakerMillOut`), or empty when it is not part of one. The game does not record sequences; this is read from the fragment names, so treat it as a strong hint rather than a guarantee.");
        animationInfo.add_property("phase", "\"in\" | \"loop\" | \"out\" | \"variation\" | \"\"", "Which part of its `sequence` this is: the way in, the middle part the body stays in, the way out, or a variation played in place of the middle. `loop` is the middle whether or not it ends by itself -- `oneShot` says that, and `playAnimation`'s `loop` repeats it. Empty when `sequence` is.");
        animationInfo.add_property("partner", "string", "The fragment a second actor plays against this one, for the game's two-person animations (`TiedUpOut_Master` for `TiedUpOut_Slave`), or empty. Each actor plays its own fragment; read from the fragment names, like `sequence`.");
        animationInfo.add_property("slaveBody", "boolean", "Whether, with these tags, the fragment also animates a second character the game binds to the body -- a corpse carried, a horse groomed, a smith's workpiece. `playAnimation` binds none, so only this body moves.");

        auto &propInfo = ServerCatalog().data_type("AnimationPropInfo", "A model the game puts in a body's hand: one of its tools, from the item tables.");
        propInfo.add_property("name", "string", "The item's name, which is what a prop's `item` takes.");
        propInfo.add_property("model", "string", "The model it is drawn with.");
        propInfo.add_property("hand", "string", "The kind of thing it is (`bucket`, `axe_tool`), which is what `AnimationInfo.rightHand` and `leftHand` name.");

        v8pp::module animations(isolate, ServerCatalog(), "Animations", "Every animation the game plays on a human body, and the props it gives one to hold. `player.playAnimation` and `npc.playAnimation` play them.");

        animations.function("list", &Animations::JS_List,
            v8pp::metadata::docs("AnimationInfo[]", {v8pp::metadata::param("search", "string", true, "Only rows whose fragment or tags contain this, ignoring case. Everything when left out.")},
                "Lists the animations the game plays on people -- over six thousand, from waves and bows to chopping wood, drinking and every tavern pose. These are the game's own combinations, so each one is known to play; `playAnimation` also takes fragments and tags this does not list, and plays whatever the body's animation database makes of them.",
                "One row per fragment and tag combination, sorted by fragment."));

        animations.function("props", &Animations::JS_Props,
            v8pp::metadata::docs("AnimationPropInfo[]", {}, "Lists the tools and carried objects the game puts in people's hands, by item name. Pick one whose `hand` matches the animation's `rightHand` or `leftHand` and it sits in the hand the way the animation was made for.", "Every prop, sorted by name."));

        animations.publish(global);
    }
} // namespace KCDC::Scripting
