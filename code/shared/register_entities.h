#pragma once

namespace KCDC::Shared::Entities {
    /**
     * Registers every replicated type with the framework's EntityRegistry.
     *
     * Called once per side before connecting, because the wire carries a type
     * id and nothing else: a peer that has not registered a name cannot build
     * the replica the other side sent. This is the server half, which
     * registers the shared entities themselves.
     *
     * The client registers subclasses of these same names -- a client replica
     * owns the native game object and so includes `sdk/`, which nothing under
     * `shared/` may do. Its half is the identical list in
     * `client/src/core/register_entities.cpp`; the two orders are kept in step
     * by hand, and the names are what has to match, not the order.
     */
    void RegisterEntities();
} // namespace KCDC::Shared::Entities
