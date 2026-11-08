#pragma once

#include "../3dengine/s_rendering_pass_info.h"
#include "i_render_aux_geom.h"
#include "s_display_context_description.h"
#include "s_display_context_key.h"
#include "s_graphics_pipeline_description.h"
#include "s_graphics_pipeline_key.h"
#include "s_render_light.h"

namespace SDK {
    /**
     * CryEngine's renderer, as far as the mod reaches into it.
     *
     * ProjectToScreen is +0x308: CScriptBind_System::ProjectToScreen
     * (0x1839A9F74) calls it there, and CD3D9Renderer's implementation
     * (0x1809356D8) matches slot 0x308 of the vtable carrying
     * StartRenderIntroMovies at slot 11.
     */
    struct IRenderer {
        virtual ~IRenderer()      = default; // +0x000
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        /** Starts the boot logo reel. Suppressed, never called by the mod. */
        virtual void StartRenderIntroMovies() = 0; // +0x058

        /**
         * Tears the reel down. False is the deterministic kill.
         *
         * The implementation guards on m_pIntroMovieRenderer (+0x108), so this
         * is a no-op when no reel exists -- it cannot be called too early or
         * too often. With `waitForFinished` it consults
         * wh_sys_SkipStartupVideoWhenPossible and, when that is clear, holds
         * the calling thread in a Sleep(10) loop until the Bink drains. With
         * false it skips that branch entirely and goes straight to destroying
         * the movie renderer, which is why nothing here has to win a race.
         */
        virtual void StopRenderIntroMovies(bool waitForFinished) = 0; // +0x060
        virtual void unk_fn0013()                                = 0;
        virtual void unk_fn0014()                                = 0;
        virtual void unk_fn0015()                                = 0;
        virtual void unk_fn0016()                                = 0;
        virtual void unk_fn0017()                                = 0;
        virtual void unk_fn0018()                                = 0;
        virtual void unk_fn0019()                                = 0;
        virtual void unk_fn0020()                                = 0;
        virtual void unk_fn0021()                                = 0;
        virtual void unk_fn0022()                                = 0;
        virtual void unk_fn0023()                                = 0;
        virtual void unk_fn0024()                                = 0;
        virtual void unk_fn0025()                                = 0;
        virtual void unk_fn0026()                                = 0;
        virtual void unk_fn0027()                                = 0;
        virtual void unk_fn0028()                                = 0;
        virtual void unk_fn0029()                                = 0;
        /**
         * A display context with no window: its colour target is a texture,
         * `$TextureRT %d:Current`, made at the description's resolution
         * (0x1824E8E74, KCD2-only -- 5.7 has none). Like every context
         * creator it ends by making the new context the active one, which
         * the next BeginFrame replaces.
         */
        virtual SDisplayContextKey *CreateTextureBackedContext(SDisplayContextKey *result, const SDisplayContextDescription &description) = 0; // +0x0F0
        virtual void unk_fn0031()                                                                                                         = 0;
        virtual bool DeleteContext(const SDisplayContextKey &key)                                                                         = 0; // +0x100, 0x1824E9B8C
        virtual void unk_fn0033()                                                                                                         = 0;
        virtual void unk_fn0034()                                                                                                         = 0;
        /** Built on the render thread; the call waits for it (0x1824E8300). */
        virtual SGraphicsPipelineKey *CreateGraphicsPipeline(SGraphicsPipelineKey *result, const SGraphicsPipelineDescription &description) = 0; // +0x118
        virtual void unk_fn0036()                                                                                                           = 0;
        /** Sizes a pipeline and a context together (0x1824FED8C), as the map screenshot does before its frame. */
        virtual void ResizePipelineAndContext(const SGraphicsPipelineKey &pipeline, const SDisplayContextKey &context, int width, int height) = 0; // +0x128
        virtual bool DeleteGraphicsPipeline(const SGraphicsPipelineKey &key)                                                                  = 0; // +0x130, 0x1824E9CBC
        virtual void unk_fn0039()                                                                                                             = 0;
        virtual void unk_fn0040()                                                                                                             = 0;
        virtual void unk_fn0041()                                                                                                             = 0;
        virtual void unk_fn0042()                                                                                                             = 0;
        virtual void unk_fn0043()                                                                                                             = 0;
        virtual void unk_fn0044()                                                                                                             = 0;
        virtual void unk_fn0045()                                                                                                             = 0;
        virtual void unk_fn0046()                                                                                                             = 0;
        virtual void unk_fn0047()                                                                                                             = 0;
        virtual void unk_fn0048()                                                                                                             = 0;
        /**
         * Opens a frame on a context and pipeline (0x18052EF84). Not
         * re-entrant: the main frame is closed by the time `CCryAction::
         * PostUpdate` returns and the next opens in `PreUpdate`, which is the
         * window a second frame fits in.
         */
        virtual void BeginFrame(const SDisplayContextKey &context, const SGraphicsPipelineKey &pipeline) = 0; // +0x188
        virtual void unk_fn0050()                                                                        = 0;
        virtual void unk_fn0051()                                                                        = 0;
        virtual void unk_fn0052()                                                                        = 0;
        virtual void unk_fn0053()                                                                        = 0;
        virtual void unk_fn0054()                                                                        = 0;
        virtual void unk_fn0055()                                                                        = 0;
        virtual void unk_fn0056()                                                                        = 0;
        virtual void unk_fn0057()                                                                        = 0;
        virtual void EndFrame()                                                                          = 0; // +0x1D0, 0x1809A7C54
        virtual void unk_fn0059()                                                                        = 0;
        virtual void unk_fn0060()                                                                        = 0;
        virtual void unk_fn0061()                                                                        = 0;
        virtual void unk_fn0062()                                                                        = 0;
        virtual void unk_fn0063()                                                                        = 0;
        virtual void unk_fn0064()                                                                        = 0;
        virtual void unk_fn0065()                                                                        = 0;
        virtual void unk_fn0066()                                                                        = 0;
        virtual void unk_fn0067()                                                                        = 0;
        virtual void unk_fn0068()                                                                        = 0;
        virtual void unk_fn0069()                                                                        = 0;
        virtual void unk_fn0070()                                                                        = 0;
        virtual void unk_fn0071()                                                                        = 0;
        virtual void unk_fn0072()                                                                        = 0;
        virtual void unk_fn0073()                                                                        = 0;
        virtual void unk_fn0074()                                                                        = 0;
        virtual void unk_fn0075()                                                                        = 0;
        virtual void unk_fn0076()                                                                        = 0;
        virtual void unk_fn0077()                                                                        = 0;
        virtual void unk_fn0078()                                                                        = 0;
        virtual void unk_fn0079()                                                                        = 0;
        virtual void unk_fn0080()                                                                        = 0;
        virtual void unk_fn0081()                                                                        = 0;
        virtual void unk_fn0082()                                                                        = 0;
        virtual void unk_fn0083()                                                                        = 0;
        virtual void unk_fn0084()                                                                        = 0;
        virtual void unk_fn0085()                                                                        = 0;
        virtual void unk_fn0086()                                                                        = 0;
        virtual void unk_fn0087()                                                                        = 0;
        virtual void unk_fn0088()                                                                        = 0;
        virtual void unk_fn0089()                                                                        = 0;
        virtual void unk_fn0090()                                                                        = 0;
        virtual void unk_fn0091()                                                                        = 0;
        virtual void unk_fn0092()                                                                        = 0;
        virtual void unk_fn0093()                                                                        = 0;
        virtual void unk_fn0094()                                                                        = 0;
        virtual void unk_fn0095()                                                                        = 0;
        virtual void unk_fn0096()                                                                        = 0;

        /**
         * Projects a world point onto the viewport. `sx`/`sy` come back as
         * percentages (0..100, y from the top) and `sz` as the projected
         * depth. False means behind the camera or off-screen, which is the
         * whole cull -- there is no frustum test to write here.
         */
        virtual bool ProjectToScreen(float ptx, float pty, float ptz, float *sx, float *sy, float *sz) = 0; // +0x308
        virtual void unk_fn0098()                                                                      = 0;
        virtual void unk_fn0099()                                                                      = 0;
        virtual void unk_fn0100()                                                                      = 0;
        virtual void unk_fn0101()                                                                      = 0;
        virtual void unk_fn0102()                                                                      = 0;
        virtual void unk_fn0103()                                                                      = 0;
        virtual void unk_fn0104()                                                                      = 0;
        /**
         * Lets the render thread catch up (0x18052FB9C). (true, true, true)
         * is CryEngine's `ForceFlushRTCommands`: it waits until every command
         * queued so far has run. A no-op on the render thread itself.
         */
        virtual bool FlushRTCommands(bool wait, bool immediately, bool force) = 0; // +0x348
        virtual void unk_fn0106()                                             = 0;
        virtual void unk_fn0107()                                             = 0;
        virtual void unk_fn0108()                                             = 0;
        virtual void unk_fn0109()                                             = 0;
        virtual void unk_fn0110()                                             = 0;
        virtual void unk_fn0111()                                             = 0;
        virtual void unk_fn0112()                                             = 0;
        virtual void unk_fn0113()                                             = 0;
        virtual void unk_fn0114()                                             = 0;
        virtual void unk_fn0115()                                             = 0;
        virtual void unk_fn0116()                                             = 0;
        virtual void unk_fn0117()                                             = 0;
        virtual void unk_fn0118()                                             = 0;
        virtual void unk_fn0119()                                             = 0;
        virtual void unk_fn0120()                                             = 0;
        virtual void unk_fn0121()                                             = 0;
        virtual void unk_fn0122()                                             = 0;
        virtual void unk_fn0123()                                             = 0;
        virtual void unk_fn0124()                                             = 0;
        virtual void unk_fn0125()                                             = 0;
        virtual void unk_fn0126()                                             = 0;
        virtual void unk_fn0127()                                             = 0;
        virtual void unk_fn0128()                                             = 0;
        virtual void unk_fn0129()                                             = 0;
        virtual void unk_fn0130()                                             = 0;
        virtual void unk_fn0131()                                             = 0;
        virtual void unk_fn0132()                                             = 0;
        virtual void unk_fn0133()                                             = 0;
        virtual void unk_fn0134()                                             = 0;
        virtual void unk_fn0135()                                             = 0;
        virtual void unk_fn0136()                                             = 0;
        virtual void unk_fn0137()                                             = 0;
        virtual void unk_fn0138()                                             = 0;
        virtual void unk_fn0139()                                             = 0;
        virtual void unk_fn0140()                                             = 0;
        virtual void unk_fn0141()                                             = 0;
        virtual void unk_fn0142()                                             = 0;
        virtual void unk_fn0143()                                             = 0;
        virtual void unk_fn0144()                                             = 0;
        virtual void unk_fn0145()                                             = 0;
        virtual void unk_fn0146()                                             = 0;
        virtual void unk_fn0147()                                             = 0;
        virtual void unk_fn0148()                                             = 0;
        virtual void unk_fn0149()                                             = 0;
        virtual void unk_fn0150()                                             = 0;
        virtual void unk_fn0151()                                             = 0;
        virtual void unk_fn0152()                                             = 0;
        virtual void unk_fn0153()                                             = 0;
        virtual void unk_fn0154()                                             = 0;
        virtual void unk_fn0155()                                             = 0;
        virtual void unk_fn0156()                                             = 0;
        virtual void unk_fn0157()                                             = 0;
        virtual void unk_fn0158()                                             = 0;
        virtual void unk_fn0159()                                             = 0;
        virtual void unk_fn0160()                                             = 0;
        /** Opens a pass's render view for writing (0x180749558). */
        virtual void EF_StartEf(const SRenderingPassInfo &pass) = 0; // +0x508
        virtual void unk_fn0162()                               = 0;
        virtual void unk_fn0163()                               = 0;
        virtual void unk_fn0164()                               = 0;
        /** Closes the pass's render view and hands it to the render thread (0x180A5C3D4). */
        virtual void EF_EndEf3D(int unknown0, int unknown1, const SRenderingPassInfo &pass, int shaderFlags) = 0; // +0x528
        virtual void unk_fn0166()                                                                            = 0;
        virtual void unk_fn0167()                                                                            = 0;
        virtual void unk_fn0168()                                                                            = 0;
        virtual void unk_fn0169()                                                                            = 0;
        virtual void unk_fn0170()                                                                            = 0;
        /** Adds a dynamic light to the pass's render view, writing its index into `light->m_id` (0x1804E4454). */
        virtual void EF_ADDDlight(SRenderLight *light, const SRenderingPassInfo &pass)                               = 0; // +0x558
        virtual void unk_fn0172()                                                                                    = 0;
        virtual void unk_fn0173()                                                                                    = 0;
        virtual int EF_AddDeferredLight(const SRenderLight &light, float multiplier, const SRenderingPassInfo &pass) = 0; // +0x570, 0x180656B14
        virtual void unk_fn0175()                                                                                    = 0;
        virtual void unk_fn0176()                                                                                    = 0;
        virtual void unk_fn0177()                                                                                    = 0;
        virtual void unk_fn0178()                                                                                    = 0;
        virtual void unk_fn0179()                                                                                    = 0;
        virtual void unk_fn0180()                                                                                    = 0;
        virtual void unk_fn0181()                                                                                    = 0;
        virtual void unk_fn0182()                                                                                    = 0;
        virtual void unk_fn0183()                                                                                    = 0;
        virtual void unk_fn0184()                                                                                    = 0;
        virtual void unk_fn0185()                                                                                    = 0;
        virtual void unk_fn0186()                                                                                    = 0;
        virtual void unk_fn0187()                                                                                    = 0;
        virtual void unk_fn0188()                                                                                    = 0;
        virtual void unk_fn0189()                                                                                    = 0;
        virtual void unk_fn0190()                                                                                    = 0;
        virtual void unk_fn0191()                                                                                    = 0;
        virtual void unk_fn0192()                                                                                    = 0;
        virtual void unk_fn0193()                                                                                    = 0;
        virtual void unk_fn0194()                                                                                    = 0;
        virtual void unk_fn0195()                                                                                    = 0;
        virtual void unk_fn0196()                                                                                    = 0;
        virtual void unk_fn0197()                                                                                    = 0;
        virtual void unk_fn0198()                                                                                    = 0;
        // +0x638, 0x1803D433C; proven by CScriptBind_System::DrawLine.
        virtual IRenderAuxGeom *GetIRenderAuxGeom()                                                                   = 0;
        virtual void unk_fn0200()                                                                                    = 0;
        virtual void unk_fn0201()                                                                                    = 0;
        virtual void unk_fn0202()                                                                                    = 0;
        virtual void unk_fn0203()                                                                                    = 0;
        virtual void unk_fn0204()                                                                                    = 0;
        virtual void unk_fn0205()                                                                                    = 0;
        virtual void unk_fn0206()                                                                                    = 0;
        virtual void unk_fn0207()                                                                                    = 0;
        virtual void unk_fn0208()                                                                                    = 0;
        virtual void unk_fn0209()                                                                                    = 0;
        virtual void unk_fn0210()                                                                                    = 0;
        virtual void unk_fn0211()                                                                                    = 0;
        virtual void unk_fn0212()                                                                                    = 0;
        virtual void unk_fn0213()                                                                                    = 0;
        virtual void unk_fn0214()                                                                                    = 0;
        virtual void unk_fn0215()                                                                                    = 0;
        virtual void unk_fn0216()                                                                                    = 0;
        virtual void unk_fn0217()                                                                                    = 0;
        virtual void unk_fn0218()                                                                                    = 0;
        virtual void unk_fn0219()                                                                                    = 0;
        virtual void unk_fn0220()                                                                                    = 0;
        virtual void unk_fn0221()                                                                                    = 0;
        virtual void unk_fn0222()                                                                                    = 0;
        virtual void unk_fn0223()                                                                                    = 0;
        virtual void unk_fn0224()                                                                                    = 0;
        virtual void unk_fn0225()                                                                                    = 0;
        virtual void unk_fn0226()                                                                                    = 0;
        virtual void unk_fn0227()                                                                                    = 0;
        /**
         * Whether ending a frame presents the active context (writes
         * renderer+0xA0; a COMDAT-folded byte setter). Off around a frame on
         * a texture-backed context: the present casts the active context to
         * the swap-chain class.
         */
        virtual void EnableSwapBuffers(bool enable) = 0; // +0x720

        /** Returns the renderer published through gEnv. */
        static IRenderer *GetInstance();

        /**
         * `CRenderer::EF_ReloadFile` (0x1824F1FA8, slot 136): re-reads one file the renderer or the
         * 3D engine holds -- a `.cgf` refreshes its geometry in place, an image reloads the texture
         * of that name. Materials are not covered; see IMaterialManager::LoadMaterialFromXml.
         */
        bool ReloadFile(const char *path);
    };
} // namespace SDK
