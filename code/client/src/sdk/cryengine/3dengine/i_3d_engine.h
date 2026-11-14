#pragma once

#include "../../types.h"
#include "../system/i_process.h"
#include "i_material_manager.h"
#include "i_merged_meshes_manager.h"
#include "i_render_node.h"
#include "i_stat_obj.h"
#include "i_stat_inst_group.h"
#include "i_time_of_day.h"

namespace SDK {
    class ITerrain;

    /**
     * Index into the engine's cached global environment parameters.
     *
     * `CScriptBind_System::GetSkyColor` (`0x1839A89E4`) calls
     * `GetGlobalParameter` with 1, which pins `E3DPARAM_SKY_COLOR`; the rest
     * follows from the shape of each case at `0x180655D1C` and agrees with
     * stock CryEngine's numbering. Only proven entries are spelled.
     *
     * Reading one proves nothing about its content.
     * `CTimeOfDay::UpdateEnvironmentLighting` (`0x1806BEC04`) is the only
     * writer and never passes 0, 1 or 3, so `SUN_COLOR`, `SKY_COLOR` and
     * `AMBIENT_GROUND_COLOR` read zero for the life of the process -- and an
     * unknown parameter leaves the destination untouched, so zero and unset
     * are indistinguishable unless the caller seeds its own out value.
     * `DAY_NIGHT_INDICATOR` is written every tick but Warhorse feeds it a
     * time-of-day variable that sits at 1.0 at two in the morning, so it does
     * not mean what stock's name says. There is no usable day/night signal
     * here; `wh::rpgmodule::C_Calendar` owns the hour.
     */
    enum E3DEngineParameter : std::int32_t {
        E3DPARAM_SUN_COLOR = 0,
        E3DPARAM_SKY_COLOR = 1,
        E3DPARAM_SUN_SPECULAR_MULTIPLIER = 2,
        E3DPARAM_AMBIENT_GROUND_COLOR = 3,
        E3DPARAM_AMBIENT_MIN_HEIGHT = 4,
        E3DPARAM_AMBIENT_MAX_HEIGHT = 5,
        E3DPARAM_SKYBOX_MULTIPLIER = 69,
        E3DPARAM_DAY_NIGHT_INDICATOR = 70,
    };

    /**
     * Main 3D-engine interface.
     *
     * RTTI proves I3DEngine derives from IProcess at primary offset +0x00.
     * Its abstract table at 0x183A6D718 contains exactly 295 entries in total.
     * KCD2 shuffles this interface; only independently proven operations are
     * named, including render-node creation/deletion and environment access.
     */
    class I3DEngine : public IProcess {
      public:
        ~I3DEngine() override = default;

        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual IStatObj *LoadStatObj(const char *fileName,
                                      const char *geometryName = nullptr,
                                      void **ppSubObject = nullptr,
                                      bool useStreaming = true,
                                      std::uint32_t loadingFlags = 0) = 0; // +0xF0
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        virtual void unk_fn0034() = 0;
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void RegisterEntity(IRenderNode *pRenderNode) = 0; // +0x130
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0;
        virtual void unk_fn0044() = 0;
        virtual void unk_fn0045() = 0;
        virtual void unk_fn0046() = 0;
        virtual void unk_fn0047() = 0;
        virtual void unk_fn0048() = 0;
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0;
        virtual void unk_fn0051() = 0;
        virtual void unk_fn0052() = 0;
        virtual void unk_fn0053() = 0;
        virtual void unk_fn0054() = 0;
        virtual void unk_fn0055() = 0;
        virtual void unk_fn0056() = 0;
        virtual void unk_fn0057() = 0;
        virtual void unk_fn0058() = 0;
        virtual void unk_fn0059() = 0;
        virtual void unk_fn0060() = 0;
        virtual void unk_fn0061() = 0;
        virtual void unk_fn0062() = 0;
        virtual void unk_fn0063() = 0;
        virtual void unk_fn0064() = 0;
        virtual void unk_fn0065() = 0;
        virtual void unk_fn0066() = 0;
        virtual void unk_fn0067() = 0;
        virtual void unk_fn0068() = 0;
        virtual void unk_fn0069() = 0;
        virtual void unk_fn0070() = 0;
        virtual void unk_fn0071() = 0;
        virtual void unk_fn0072() = 0;
        virtual void SetRainParams(const SRainParams &params) = 0; // +0x248
        virtual bool GetRainParams(SRainParams &params) = 0;       // +0x250

        /**
         * Snow volume and snowfall parameters, immediately after the rain
         * pair. The setters are `0x18349C8C8` and `0x18349C86C`; the getters
         * are `0x180E88FE4` and `0x180E89058`. They access `CObjManager`'s
         * `m_snowParams` at +0x200 and do nothing before a level is loaded.
         *
         * **Reachable, but inert. Do not build on these.** Setting the
         * parameters renders no snow, including with `r_Snow` set to 1 or 2.
         * The failure is downstream of the setters; its cause is unresolved.
         * The shipped shader cache lacks `deferredsnow@deferredsnowps`, and
         * no game path calls these setters.
         *
         * `CSnowStage` is built with the pipeline by ctor `0x180742D90`,
         * occupying 0x3F30 bytes at pipeline +0x108. Its `Init` loads snow
         * textures, its `Update` copies 56 bytes of `SSnowParams` per frame,
         * and the main pass gates the deferred G-buffer and displacement
         * passes. These paths alone do not produce visible snow.
         *
         * The volume is centred, not global: the shader attenuates over
         * `radius` from `center`. `snowAmount` is a coverage exponent clamped
         * to 10 by the shader rather than an opacity, `frostAmount` and
         * `surfaceFreezing` saturate to 0..1, and `flakeCount` is flakes per
         * cluster against `r_SnowFlakeClusters` clusters. The flake pass is
         * additionally gated on `m_bDeferredSnowEnabled`, so there are no
         * flakes without surface snow.
         */
        virtual void SetSnowSurfaceParams(const Vec3 &center, float radius, float snowAmount, float frostAmount, float surfaceFreezing) = 0;    // +0x258
        virtual bool GetSnowSurfaceParams(Vec3 &center, float &radius, float &snowAmount, float &frostAmount, float &surfaceFreezing) = 0;      // +0x260
        virtual void SetSnowFallParams(int flakeCount, float flakeSize, float brightness, float gravityScale, float windScale, float turbulence, float turbulenceFrequency) = 0;    // +0x268
        virtual bool GetSnowFallParams(int &flakeCount, float &flakeSize, float &brightness, float &gravityScale, float &windScale, float &turbulence, float &turbulenceFrequency) = 0; // +0x270
        virtual void unk_fn0079() = 0;
        virtual void unk_fn0080() = 0;
        virtual void unk_fn0081() = 0;
        virtual void unk_fn0082() = 0;
        virtual void unk_fn0083() = 0;
        virtual void unk_fn0084() = 0;
        virtual void unk_fn0085() = 0;
        virtual void unk_fn0086() = 0;
        virtual void unk_fn0087() = 0;
        virtual void unk_fn0088() = 0;
        virtual void unk_fn0089() = 0;
        virtual void unk_fn0090() = 0;
        virtual void unk_fn0091() = 0;
        virtual void unk_fn0092() = 0;
        virtual void unk_fn0093() = 0;
        virtual void unk_fn0094() = 0;
        virtual void unk_fn0095() = 0;
        virtual void unk_fn0096() = 0;
        virtual void unk_fn0097() = 0;
        virtual float GetTerrainElevation(float x, float y) = 0; // +0x310, C_UIMap::PlaceCheckpointAtCursor
        virtual void unk_fn0099() = 0;
        virtual void unk_fn0100() = 0;
        virtual void unk_fn0101() = 0;
        virtual int GetTerrainSize() = 0; // +0x330, same bounds used by GetTerrainElevation
        virtual void unk_fn0103() = 0;
        virtual void unk_fn0104() = 0;
        virtual void unk_fn0105() = 0;
        virtual void unk_fn0106() = 0;
        virtual void unk_fn0107() = 0;
        virtual void unk_fn0108() = 0;
        virtual void unk_fn0109() = 0;
        virtual void unk_fn0110() = 0;
        virtual void unk_fn0111() = 0;
        virtual bool SetStatInstGroup(int groupId,
                                      const IStatInstGroup &group) = 0; // +0x380
        virtual bool GetStatInstGroup(int groupId,
                                      IStatInstGroup &group) = 0; // +0x388
        virtual void unk_fn0114() = 0;
        virtual void unk_fn0115() = 0;
        virtual void unk_fn0116() = 0;
        virtual void unk_fn0117() = 0;
        virtual void unk_fn0118() = 0;
        virtual void unk_fn0119() = 0;
        virtual void unk_fn0120() = 0;
        virtual void unk_fn0121() = 0;
        virtual void unk_fn0122() = 0;
        virtual void unk_fn0123() = 0;
        virtual void unk_fn0124() = 0;
        virtual void unk_fn0125() = 0;
        virtual void unk_fn0126() = 0;
        virtual void unk_fn0127() = 0;
        virtual void unk_fn0128() = 0;
        virtual void unk_fn0129() = 0;
        virtual void unk_fn0130() = 0;
        virtual void unk_fn0131() = 0;
        virtual void unk_fn0132() = 0;
        virtual void unk_fn0133() = 0;
        virtual void unk_fn0134() = 0;
        virtual void unk_fn0135() = 0;
        virtual void unk_fn0136() = 0;
        virtual void unk_fn0137() = 0;
        virtual void unk_fn0138() = 0;
        virtual void unk_fn0139() = 0;
        virtual void unk_fn0140() = 0;
        virtual void unk_fn0141() = 0;
        virtual void unk_fn0142() = 0;
        virtual void unk_fn0143() = 0;
        virtual void unk_fn0144() = 0;
        virtual void unk_fn0145() = 0;
        virtual void unk_fn0146() = 0;
        virtual void unk_fn0147() = 0;
        virtual void unk_fn0148() = 0;
        virtual void unk_fn0149() = 0;
        virtual void unk_fn0150() = 0;
        virtual void unk_fn0151() = 0;
        virtual void unk_fn0152() = 0;
        virtual void unk_fn0153() = 0;
        virtual void unk_fn0154() = 0;
        virtual void unk_fn0155() = 0;
        virtual IRenderNode *CreateRenderNode(EERType type) = 0; // +0x4E0
        virtual void DeleteRenderNode(IRenderNode *pRenderNode) = 0; // +0x4E8

        /**
         * Sets the global wind, and with it the engine's own wind area.
         *
         * `0x1805533C8` stores the vector at `C3DEngine+0x290`, mirrors it
         * into the renderer's shared environment and creates or updates a
         * `PE_AREA` physical entity from it, so this reaches cloth, ropes,
         * vegetation and particles rather than only the shaders.
         *
         * `CScriptBind_System::SetWind` (`0x1839AC47C`) is its only caller in
         * the whole module: the game never writes the wind at runtime, so
         * whatever is set here stands until the next level loads.
         */
        virtual void SetWind(const Vec3 &wind) = 0; // +0x4F0
        virtual void unk_fn0159() = 0;

        /**
         * The global wind, or zero when `e_Wind` is off or `indoors` is set.
         *
         * `0x1805D5538` returns the origin rather than the stored vector in
         * both of those cases, so a value read back here is not proof of what
         * was written.
         */
        virtual Vec3 GetWind(bool indoors) = 0; // +0x500
        virtual void unk_fn0161() = 0;
        virtual void unk_fn0162() = 0;
        virtual void unk_fn0163() = 0;
        virtual void unk_fn0164() = 0;
        virtual void unk_fn0165() = 0;
        virtual void unk_fn0166() = 0;
        virtual void unk_fn0167() = 0;
        virtual void unk_fn0168() = 0;
        virtual void unk_fn0169() = 0;
        virtual void unk_fn0170() = 0;

        /**
         * Writes one float post-effect parameter by name (`0x180664278`,
         * forwarding to `CD3D9Renderer::EF_SetPostEffectParam` at renderer
         * +0x588 and on to `CParamFloat::SetParam` at `0x1806642E8`). An
         * unknown name is ignored.
         *
         * The parameter is stock CryEngine's `CParamFloat`, unchanged: a
         * forced write sets the value at once; an unforced one is summed with
         * every other unforced write of the same frame, averaged when the
         * render thread syncs, and approached at
         * `frameTime * r_PostprocessParamsBlendingTimeScale` per frame. Either
         * way the value then stands until somebody writes it again -- nothing
         * decays it back to its default.
         *
         * Every value lives in two thread slots, so call from the game thread.
         */
        virtual void SetPostEffectParam(const char *param, float value, bool forceValue) const = 0; // +0x558
        virtual void unk_fn0172() = 0;
        virtual void unk_fn0173() = 0;

        /** Reads the game thread's current value of one float post-effect parameter (`0x180C043D0`). An unknown name leaves `value` untouched. */
        virtual void GetPostEffectParam(const char *param, float &value) const = 0; // +0x570
        virtual void unk_fn0175() = 0;
        virtual void unk_fn0176() = 0;
        virtual void unk_fn0177() = 0;
        virtual void unk_fn0178() = 0;
        virtual void unk_fn0179() = 0;
        virtual void unk_fn0180() = 0;
        virtual void unk_fn0181() = 0;
        virtual void unk_fn0182() = 0;
        virtual void unk_fn0183() = 0;
        virtual void unk_fn0184() = 0;
        virtual void unk_fn0185() = 0;
        virtual void unk_fn0186() = 0;
        virtual void unk_fn0187() = 0;
        virtual void unk_fn0188() = 0;
        virtual void unk_fn0189() = 0;
        virtual void unk_fn0190() = 0;
        virtual void unk_fn0191() = 0;
        virtual void unk_fn0192() = 0;
        virtual void unk_fn0193() = 0;
        virtual void unk_fn0194() = 0;
        virtual void unk_fn0195() = 0;
        virtual void unk_fn0196() = 0;

        /** Reads back a global, so it is valid from engine init onward. */
        virtual IMaterialManager *GetMaterialManager() = 0; // +0x628
        virtual void unk_fn0198() = 0;
        virtual void unk_fn0199() = 0;
        virtual void unk_fn0200() = 0;
        virtual void unk_fn0201() = 0;
        virtual void unk_fn0202() = 0;
        virtual void unk_fn0203() = 0;
        virtual void unk_fn0204() = 0;
        virtual ITerrain *GetITerrain() = 0; // +0x668
        virtual void unk_fn0206() = 0;
        virtual void unk_fn0207() = 0;
        virtual void unk_fn0208() = 0;

        /**
         * The owner of the terrain's merged-mesh sectors. `0x180E3C710`
         * reads back a global, as `C_RespawnManager`'s destructor
         * (0x182ADCCDC) does to take its stream listener off; null until
         * the engine has made it.
         */
        virtual IMergedMeshesManager *GetIMergedMeshesManager() = 0; // +0x688
        virtual void unk_fn0210() = 0;
        virtual void unk_fn0211() = 0;
        virtual void unk_fn0212() = 0;
        virtual ITimeOfDay *GetTimeOfDay() = 0; // +0x6A8
        virtual void unk_fn0214() = 0;
        virtual void unk_fn0215() = 0;
        virtual void unk_fn0216() = 0;
        virtual void unk_fn0217() = 0;
        virtual void unk_fn0218() = 0;
        virtual void unk_fn0219() = 0;
        virtual void unk_fn0220() = 0;
        virtual void unk_fn0221() = 0;
        virtual void unk_fn0222() = 0;
        virtual void unk_fn0223() = 0;
        virtual void unk_fn0224() = 0;

        /**
         * Copies one cached global environment parameter into `out`.
         *
         * A switch over fields already standing on the engine: a virtual call
         * and twelve bytes of copy. Parameters it does not know leave `out`
         * alone, so seed it before the call if unset has to be detectable.
         */
        virtual void GetGlobalParameter(E3DEngineParameter param, Vec3 &out) = 0; // +0x708
        virtual void unk_fn0226() = 0;
        virtual void unk_fn0227() = 0;
        virtual void unk_fn0228() = 0;
        virtual void unk_fn0229() = 0;
        virtual void unk_fn0230() = 0;
        virtual void unk_fn0231() = 0;
        virtual void unk_fn0232() = 0;
        virtual void unk_fn0233() = 0;
        virtual void unk_fn0234() = 0;
        virtual void unk_fn0235() = 0;
        virtual void unk_fn0236() = 0;
        virtual void unk_fn0237() = 0;
        virtual void unk_fn0238() = 0;
        virtual void unk_fn0239() = 0;
        virtual void unk_fn0240() = 0;
        virtual void unk_fn0241() = 0;
        virtual void unk_fn0242() = 0;
        /**
         * Every render node whose bounds meet `box`, from the octree and the
         * vis areas, so indoor geometry is included. The only way to reach a
         * node that was never physicalized, which most decoration is not.
         *
         * Called twice: null for the count, then an array of at least that
         * size -- it memcpy's its whole list and takes no size, so a shorter
         * array is written past the end of. The engine owns and frees its own
         * vector, so nothing crosses the heap boundary.
         */
        virtual std::uint32_t GetObjectsInBox(const AABB &box, IRenderNode **out) = 0; // +0x798
        virtual void unk_fn0244() = 0;
        virtual void unk_fn0245() = 0;
        virtual void unk_fn0246() = 0;
        virtual void unk_fn0247() = 0;
        /**
         * Tells the engine a render node changed in a way its cached draw
         * state cannot see (0x18349509C, C3DEngine table 0x1840205E8). With
         * `0x40000008` in the flags it also schedules the cached shadows for a
         * rebuild; whatever the flags, it then does CryEngine's inline
         * `IRenderNode::InvalidatePermanentRenderObject` -- the node's temp
         * data (+0x30) takes `invalidRenderObjects = hasValidRenderObjects`
         * and its manipulation frame (+0x38) goes to -1 -- so the node's
         * permanent render objects are rebuilt on its next draw.
         *
         * A character's rigid attachments are drawn into its entity render
         * proxy's permanent objects, so a `CCGFAttachment` rebound to a
         * different mesh keeps drawing the old one until this is called.
         */
        virtual void OnObjectModified(IRenderNode *renderNode, std::uint32_t flags) = 0; // +0x7C0
        virtual void unk_fn0249() = 0;
        virtual void unk_fn0250() = 0;
        virtual void unk_fn0251() = 0;
        virtual void unk_fn0252() = 0;
        virtual void unk_fn0253() = 0;
        virtual void unk_fn0254() = 0;
        virtual void unk_fn0255() = 0;
        virtual void unk_fn0256() = 0;
        virtual void unk_fn0257() = 0;
        virtual void unk_fn0258() = 0;
        virtual void unk_fn0259() = 0;
        virtual void unk_fn0260() = 0;
        virtual void unk_fn0261() = 0;
        virtual void unk_fn0262() = 0;
        virtual void unk_fn0263() = 0;
        virtual void unk_fn0264() = 0;
        virtual void unk_fn0265() = 0;
        virtual void unk_fn0266() = 0;
        virtual void unk_fn0267() = 0;
        virtual void unk_fn0268() = 0;
        virtual void unk_fn0269() = 0;
        virtual void unk_fn0270() = 0;
        virtual void unk_fn0271() = 0;
        virtual void unk_fn0272() = 0;
        virtual void unk_fn0273() = 0;
        virtual void unk_fn0274() = 0;
        virtual void unk_fn0275() = 0;
        virtual void unk_fn0276() = 0;
        virtual void unk_fn0277() = 0;
        virtual void unk_fn0278() = 0;
        virtual void unk_fn0279() = 0;
        virtual void unk_fn0280() = 0;
        virtual void unk_fn0281() = 0;
        virtual void unk_fn0282() = 0;
        virtual void unk_fn0283() = 0;
        virtual void unk_fn0284() = 0;
        virtual void unk_fn0285() = 0;
        virtual void unk_fn0286() = 0;
        virtual void unk_fn0287() = 0;
        virtual void unk_fn0288() = 0;
        virtual void unk_fn0289() = 0;
        virtual void unk_fn0290() = 0;
        virtual void unk_fn0291() = 0;
        virtual void unk_fn0292() = 0;
        virtual void unk_fn0293() = 0;
        virtual void unk_fn0294() = 0;

        static I3DEngine *GetInstance();
    };

    static_assert(sizeof(I3DEngine) == 0x8, "I3DEngine size mismatch");
} // namespace SDK
