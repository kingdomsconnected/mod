#pragma once

namespace KCDC::Core {
    /**
     * Registers every replicated type with the framework's EntityRegistry.
     *
     * Called once from Application::PostInit, before anything can connect: the
     * wire carries a type id and nothing else, so a replica of a name this
     * peer never registered cannot be built.
     *
     * This is the client half of `shared/register_entities.cpp`. It registers
     * the same eight names, but bound to the client subclasses -- the replicas
     * that own the native game object -- which is why the list cannot simply
     * be the shared one: a client replica includes `sdk/`, and nothing under
     * `shared/` may.
     */
    void RegisterEntities();
} // namespace KCDC::Core
