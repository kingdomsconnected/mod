#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>

namespace Framework::Graphics {
    class D3D12Backend;
}

namespace KCDC::Features::Prop {
    /**
     * One asynchronous back-buffer readback, followed by worker-thread PNG
     * encoding. Capture metadata describes the copied frame, never a promise
     * that the latest editor document revision has reached that frame.
     */
    class EditorCapture final {
      public:
        static EditorCapture &Get();

        /** Zero means busy or shut down. At most four completed results survive. */
        std::uint64_t Request(bool includeOverlays = false);
        nlohmann::json GetResult(std::uint64_t id) const;
        bool Cancel(std::uint64_t id);

        /** Render thread, under the existing present/resize mutex. */
        void Poll();
        void Record(Framework::Graphics::D3D12Backend &backend, bool overlaysRendered);

        /** Release capture references before DXGI resizes. False means GPU busy. */
        bool PrepareResize();

        /** Call before renderer teardown; permanently rejects new requests. */
        void Shutdown();

      private:
        EditorCapture();
        ~EditorCapture();
        EditorCapture(const EditorCapture &)            = delete;
        EditorCapture &operator=(const EditorCapture &) = delete;

        struct State;
        std::unique_ptr<State> _state;
        void EncodeLoop();
    };
} // namespace KCDC::Features::Prop
