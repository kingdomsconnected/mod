#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Minimal global CryThread worker interface used by CAsyncCommandQueue. */
    class IThread {
      public:
        virtual ~IThread() = default;
        virtual void ThreadEntry() = 0;
    };
} // namespace SDK

namespace SDK::NCryDX12 {
    class CDevice;
    class CView;
    struct CommandQueueStateLayout;

    /**
     * Intrusive reference-counted base shared by the native DX12 wrappers.
     *
     * RTTI gives this class one virtual destructor slot. Every recovered
     * resource/view/PSO constructor writes the signed reference count at
     * +0x08, and the folded deleting destructor proves the 0x10-byte extent.
     */
    class CRefCounted {
      public:
        virtual ~CRefCounted() = default;

        std::int32_t GetReferenceCount() const { return m_referenceCount; }

      protected:
        std::int32_t m_referenceCount; // 0x08
        std::uint32_t m_padding000C;
    };

    /**
     * Common base for objects owned by NCryDX12::CDevice.
     *
     * Its RTTI hierarchy is exactly CDeviceObject -> CRefCounted. Derived
     * resource, descriptor-heap, view, and PSO constructors all store their
     * owning CDevice pointer at +0x10. The scalar deleting destructor passes
     * the exact 0x18-byte class extent to the allocator.
     */
    class CDeviceObject : public CRefCounted {
      public:
        ~CDeviceObject() override = default;

        CDevice *GetDevice() const { return m_pDevice; }

      protected:
        CDevice *m_pDevice; // 0x10
    };

    /** ABI copy of D3D12_QUERY_HEAP_DESC without a Windows SDK dependency. */
    struct QueryHeapDescriptionLayout {
        std::uint32_t type;     // 0x00, D3D12_QUERY_HEAP_TYPE
        std::uint32_t count;    // 0x04
        std::uint32_t nodeMask; // 0x08
        std::uint32_t padding000C;
    };

    /**
     * Device-owned D3D12 query-heap wrapper.
     *
     * RTTI proves CDeviceObject as the direct base. Construction initializes
     * the retained native object at +0x18. Initialization calls
     * ID3D12Device3::CreateQueryHeap and copies the exact 0x0C-byte
     * D3D12_QUERY_HEAP_DESC into +0x20. Destruction releases the native heap,
     * and sized deletion proves 0x30 bytes. CDevice embeds two instances at
     * +0xB8 and +0xE8.
     */
    class CQueryHeap : public CDeviceObject {
      public:
        ~CQueryHeap() override = default;

        void *GetNativeQueryHeap() const { return m_pNativeQueryHeap; }
        const QueryHeapDescriptionLayout &GetDescription() const {
            return m_description;
        }

      protected:
        void *m_pNativeQueryHeap;                 // 0x18
        QueryHeapDescriptionLayout m_description; // 0x20
    };

    /** ABI copy of D3D12_SAMPLER_DESC without a Windows SDK dependency. */
    struct SamplerDescriptionLayout {
        std::uint32_t filter;             // 0x00, D3D12_FILTER
        std::uint32_t addressU;           // 0x04
        std::uint32_t addressV;           // 0x08
        std::uint32_t addressW;           // 0x0C
        float mipLodBias;                 // 0x10
        std::uint32_t maxAnisotropy;      // 0x14
        std::uint32_t comparisonFunction; // 0x18
        float borderColor[4];             // 0x1C
        float minLod;                     // 0x2C
        float maxLod;                     // 0x30
    };

    /**
     * Device-owned D3D12 sampler descriptor.
     *
     * RTTI proves CDeviceObject as the direct base. Initialization copies an
     * exact 0x34-byte D3D12_SAMPLER_DESC to +0x20, allocates a CPU descriptor,
     * asks the native device to create the sampler into its handle at +0x18,
     * and sized deletion proves the exact 0x58-byte extent.
     */
    class CSamplerState : public CDeviceObject {
      public:
        ~CSamplerState() override = default;

        std::uint64_t GetCpuDescriptorHandle() const {
            return m_cpuDescriptorHandle;
        }
        const SamplerDescriptionLayout &GetDescription() const {
            return m_description;
        }

      protected:
        std::uint64_t m_cpuDescriptorHandle;       // 0x18
        SamplerDescriptionLayout m_description;   // 0x20
        std::uint32_t m_padding0054;
    };

    /**
     * Device-owned immutable shader-bytecode wrapper.
     *
     * RTTI proves CDeviceObject as the direct base. Creation allocates and
     * copies the complete input bytecode into +0x18/+0x20, computes its 64-bit
     * content hash at +0x28, and sized deletion proves the exact 0x30 bytes.
     */
    class CShader : public CDeviceObject {
      public:
        ~CShader() override = default;

        const void *GetBytecode() const { return m_pBytecode; }
        std::size_t GetBytecodeSize() const { return m_bytecodeSize; }
        std::uint64_t GetBytecodeHash() const { return m_bytecodeHash; }

      protected:
        void *m_pBytecode;                // 0x18, owned copy
        std::size_t m_bytecodeSize;       // 0x20
        std::uint64_t m_bytecodeHash;     // 0x28
    };

    /** ABI copy of D3D12_INPUT_ELEMENT_DESC without a Windows SDK dependency. */
    struct InputElementDescriptionLayout {
        const char *semanticName;             // 0x00
        std::uint32_t semanticIndex;           // 0x08
        std::uint32_t format;                  // 0x0C, DXGI_FORMAT
        std::uint32_t inputSlot;               // 0x10
        std::uint32_t alignedByteOffset;       // 0x14
        std::uint32_t inputSlotClass;          // 0x18
        std::uint32_t instanceDataStepRate;    // 0x1C
    };

    /**
     * Immutable native D3D12 input-layout description.
     *
     * RTTI proves CRefCounted plus an empty NoCopy base. Construction copies
     * the input-element array into the vector at +0x10, duplicates every
     * semantic name into the parallel owner vector at +0x28, and rewrites the
     * copied descriptors to point at those stable strings. Sized deletion
     * proves the exact 0x40-byte extent.
     */
    class CInputLayout : public CRefCounted {
      public:
        ~CInputLayout() override = default;

        const InputElementDescriptionLayout *GetElementsBegin() const {
            return m_elementsBegin;
        }
        const InputElementDescriptionLayout *GetElementsEnd() const {
            return m_elementsEnd;
        }
        std::size_t GetElementCount() const {
            return m_elementsBegin
                       ? static_cast<std::size_t>(m_elementsEnd -
                                                  m_elementsBegin)
                       : 0;
        }

      protected:
        InputElementDescriptionLayout *m_elementsBegin;    // 0x10
        InputElementDescriptionLayout *m_elementsEnd;      // 0x18
        InputElementDescriptionLayout *m_elementsCapacity; // 0x20
        const char **m_semanticNamesBegin;                  // 0x28
        const char **m_semanticNamesEnd;                    // 0x30
        const char **m_semanticNamesCapacity;               // 0x38
    };

    /** ABI copy of D3D12_DESCRIPTOR_RANGE without a Windows SDK dependency. */
    struct DescriptorRangeLayout {
        std::uint32_t rangeType;                // 0x00
        std::uint32_t descriptorCount;          // 0x04
        std::uint32_t baseShaderRegister;       // 0x08
        std::uint32_t registerSpace;            // 0x0C
        std::uint32_t descriptorTableOffset;    // 0x10
    };

    /** ABI copy of D3D12_ROOT_DESCRIPTOR_TABLE. */
    struct RootDescriptorTableLayout {
        std::uint32_t descriptorRangeCount;     // 0x00
        std::uint32_t padding0004;
        const DescriptorRangeLayout *ranges;    // 0x08
    };

    /** ABI copy of D3D12_ROOT_CONSTANTS. */
    struct RootConstantsLayout {
        std::uint32_t shaderRegister;           // 0x00
        std::uint32_t registerSpace;            // 0x04
        std::uint32_t valueCount;               // 0x08
    };

    /** ABI copy of D3D12_ROOT_DESCRIPTOR. */
    struct RootDescriptorLayout {
        std::uint32_t shaderRegister;           // 0x00
        std::uint32_t registerSpace;            // 0x04
    };

    /** ABI copy of D3D12_ROOT_PARAMETER. */
    struct RootParameterLayout {
        std::uint32_t parameterType;            // 0x00
        std::uint32_t padding0004;
        union {
            RootDescriptorTableLayout descriptorTable;
            RootConstantsLayout constants;
            RootDescriptorLayout descriptor;
        };                                      // 0x08
        std::uint32_t shaderVisibility;         // 0x18
        std::uint32_t padding001C;
    };

    /** ABI copy of D3D12_STATIC_SAMPLER_DESC. */
    struct StaticSamplerDescriptionLayout {
        std::uint32_t shaderRegister;            // 0x00
        std::uint32_t registerSpace;              // 0x04
        std::uint32_t filter;                     // 0x08
        std::uint32_t addressU;                   // 0x0C
        std::uint32_t addressV;                   // 0x10
        std::uint32_t addressW;                   // 0x14
        float mipLodBias;                         // 0x18
        std::uint32_t maxAnisotropy;              // 0x1C
        std::uint32_t comparisonFunction;         // 0x20
        std::uint32_t borderColor;                // 0x24
        float minLod;                             // 0x28
        float maxLod;                             // 0x2C
        std::uint32_t shaderVisibility;           // 0x30
    };

    /**
     * Fixed-capacity root-signature description owned by CRootSignature.
     *
     * The copy helper relocates descriptor-table range pointers into the
     * leading range array. Native creation then presents the parameter and
     * static-sampler arrays as an exact D3D12_ROOT_SIGNATURE_DESC.
     */
    struct RootSignatureDescriptionLayout {
        DescriptorRangeLayout descriptorRanges[128];       // 0x0000
        std::uint32_t descriptorRangeCount;                 // 0x0A00
        std::uint32_t padding0A04;
        RootParameterLayout parameters[64];                 // 0x0A08
        std::uint32_t parameterCount;                       // 0x1208
        StaticSamplerDescriptionLayout staticSamplers[16];  // 0x120C
        std::uint32_t staticSamplerCount;                   // 0x154C
        std::uint32_t flags;                                // 0x1550
        std::uint32_t padding1554;
    };

    /**
     * Device-owned native D3D12 root signature.
     *
     * RTTI proves CDeviceObject as the direct base. Creation copies and
     * pointer-relocates the complete fixed-capacity description at +0x28,
     * serializes it, and retains the created ID3D12RootSignature at +0x20.
     * The node mask and description hash live at +0x18/+0x1C, and sized
     * deletion proves the exact 0x1580-byte extent.
     */
    class CRootSignature : public CDeviceObject {
      public:
        ~CRootSignature() override = default;

        std::uint32_t GetNodeMask() const { return m_nodeMask; }
        std::uint32_t GetDescriptionHash() const {
            return m_descriptionHash;
        }
        void *GetNativeRootSignature() const {
            return m_pNativeRootSignature;
        }
        const RootSignatureDescriptionLayout &GetDescription() const {
            return m_description;
        }

      protected:
        std::uint32_t m_nodeMask;                           // 0x0018
        std::uint32_t m_descriptionHash;                    // 0x001C
        void *m_pNativeRootSignature;                       // 0x0020
        RootSignatureDescriptionLayout m_description;      // 0x0028
    };

    /** ABI copy of D3D12_SHADER_BYTECODE. */
    struct ShaderBytecodeLayout {
        const void *bytecode;               // 0x00
        std::size_t bytecodeLength;         // 0x08
    };

    /** ABI copy of D3D12_CACHED_PIPELINE_STATE. */
    struct CachedPipelineStateLayout {
        const void *cachedBlob;             // 0x00
        std::size_t cachedBlobSize;         // 0x08
    };

    /** ABI copy of D3D12_COMPUTE_PIPELINE_STATE_DESC. */
    struct ComputePipelineStateDescriptionLayout {
        void *rootSignature;                       // 0x00
        ShaderBytecodeLayout computeShader;        // 0x08
        std::uint32_t nodeMask;                    // 0x18
        std::uint32_t padding001C;
        CachedPipelineStateLayout cachedState;     // 0x20
        std::uint32_t flags;                       // 0x30
        std::uint32_t padding0034;
    };

    /**
     * Common native D3D12 pipeline-state wrapper.
     *
     * Both concrete PSO RTTI hierarchies include CPSO between CDeviceObject
     * and the derivative. Their destruction paths release the retained native
     * pipeline state at +0x20 before destroying CDeviceObject.
     */
    class CPSO : public CDeviceObject {
      public:
        ~CPSO() override = default;

        void *GetNativePipelineState() const {
            return m_pNativePipelineState;
        }

      protected:
        std::byte m_padding0018[0x8];
        void *m_pNativePipelineState;              // 0x20
    };

    /**
     * Native D3D12 compute pipeline-state wrapper.
     *
     * Initialization copies an exact compute PSO description to +0x28 and
     * creates or loads the retained native pipeline state through the device's
     * pipeline library. Sized deletion proves the exact 0x60-byte extent.
     */
    class CComputePSO : public CPSO {
      public:
        ~CComputePSO() override = default;

        const ComputePipelineStateDescriptionLayout &GetDescription() const {
            return m_description;
        }

      protected:
        ComputePipelineStateDescriptionLayout m_description; // 0x28
    };

    /**
     * CryDX12's renderer-normalized graphics pipeline description.
     *
     * The graphics pipeline builder copies exactly 0x290 bytes into the
     * concrete wrapper. Its internal representation is intentionally kept
     * opaque until the renderer-specific packing is fully proven.
     */
    struct GraphicsPipelineStateDescriptionLayout {
        std::byte data[0x290];
    };

    /**
     * Native D3D12 graphics pipeline-state wrapper.
     *
     * RTTI proves CGraphicsPSO -> CPSO -> CDeviceObject -> CRefCounted.
     * Construction installs the common device/native-state prefix and the
     * renderer copies its normalized 0x290-byte graphics description to
     * +0x28. Sized deletion proves the exact 0x2B8-byte extent.
     */
    class CGraphicsPSO : public CPSO {
      public:
        ~CGraphicsPSO() override = default;

        const GraphicsPipelineStateDescriptionLayout &GetDescription() const {
            return m_description;
        }

      protected:
        GraphicsPipelineStateDescriptionLayout m_description; // 0x28
    };

    /** ABI copy of D3D12_RESOURCE_DESC without a Windows SDK dependency. */
    struct ResourceDescriptionLayout {
        std::uint32_t dimension;       // 0x00, D3D12_RESOURCE_DIMENSION
        std::uint32_t padding0004;
        std::uint64_t alignment;       // 0x08
        std::uint64_t width;           // 0x10
        std::uint32_t height;          // 0x18
        std::uint16_t depthOrArraySize; // 0x1C
        std::uint16_t mipLevels;       // 0x1E
        std::uint32_t format;          // 0x20, DXGI_FORMAT
        std::uint32_t sampleCount;     // 0x24
        std::uint32_t sampleQuality;   // 0x28
        std::uint32_t layout;          // 0x2C, D3D12_TEXTURE_LAYOUT
        std::uint32_t flags;           // 0x30, D3D12_RESOURCE_FLAGS
        std::uint32_t padding0034;
    };

    /**
     * Common native D3D12 buffer/texture resource wrapper.
     *
     * RTTI proves CDeviceObject plus an empty NoCopy base. Construction paths
     * copy the exact 0x38-byte D3D12_RESOURCE_DESC into +0x18. Destruction
     * releases the retained native resource at +0x70 and residency/allocation
     * state before the CDeviceObject base. Initialization caches a buffer's
     * GPU virtual address, native heap type and node masks, DXGI format plane
     * count, and the resulting total subresource count. Sized deletion proves
     * the exact 0xB8-byte extent.
     */
    class CResource : public CDeviceObject {
      public:
        ~CResource() override = default;

        const ResourceDescriptionLayout &GetDescription() const {
            return m_description;
        }

        void *GetNativeResource() const { return m_pNativeResource; }
        std::uint64_t GetGpuVirtualAddress() const {
            return m_gpuVirtualAddress;
        }
        std::uint32_t GetHeapType() const { return m_heapType; }
        std::uint32_t GetCreationNodeMask() const {
            return m_creationNodeMask;
        }
        std::uint32_t GetVisibleNodeMask() const {
            return m_visibleNodeMask;
        }
        std::uint32_t GetSubresourceCount() const {
            return m_subresourceCount;
        }
        std::uint8_t GetPlaneCount() const { return m_planeCount; }
        std::uint32_t GetCurrentState() const { return m_currentState; }
        std::uint32_t GetAnnouncedState() const { return m_announcedState; }

      protected:
        ResourceDescriptionLayout m_description; // 0x18
        std::uint32_t *m_subresourceStatesBegin;  // 0x50
        std::uint32_t *m_subresourceStatesEnd;    // 0x58
        std::uint32_t *m_subresourceStatesCapacity; // 0x60
        std::uint64_t m_gpuVirtualAddress;          // 0x68, buffers only
        void *m_pNativeResource;                  // 0x70
        std::uint64_t m_state0078;
        std::uint32_t m_heapType;                  // 0x80, D3D12_HEAP_TYPE
        std::uint32_t m_state0084;
        CView *m_pResourceView;                    // 0x88, intrusive reference
        void *m_state0090;
        std::uint32_t m_creationNodeMask;          // 0x98
        std::uint32_t m_visibleNodeMask;           // 0x9C
        std::uint32_t m_state00A0;
        std::uint32_t m_subresourceCount;          // 0xA4
        std::uint32_t m_currentState;              // 0xA8
        std::uint32_t m_announcedState;            // 0xAC
        std::uint32_t m_flags00B0;
        std::uint8_t m_planeCount;                  // 0xB4
        std::uint8_t m_state00B5;
        std::byte m_padding00B6[0x2];
    };

    /**
     * Native D3D12 resource-view/descriptor wrapper.
     *
     * RTTI proves CRefCounted plus an empty NoCopy base. The resource owner at
     * +0x10 is retained, the byte at +0x18 selects the descriptor release path
     * for view kinds 2 through 5, and +0x20 holds the live descriptor owner.
     * The deleting destructor proves the exact 0x50-byte extent.
     */
    class CView : public CRefCounted {
      public:
        ~CView() override = default;

        CResource *GetResource() const { return m_pResource; }
        std::uint8_t GetViewType() const { return m_viewType; }
        void *GetDescriptorOwner() const { return m_pDescriptorOwner; }

      protected:
        CResource *m_pResource;      // 0x10
        std::uint8_t m_viewType;     // 0x18
        std::byte m_state0019[0x3];
        std::uint32_t m_flags001C;
        void *m_pDescriptorOwner;    // 0x20
        std::byte m_description0028[0x28];
    };

    /**
     * Device-owned D3D12 descriptor heap or sub-range.
     *
     * Constructors prove CPU/GPU heap-start handles, heap type/flags, retained
     * native heap, descriptor increment size, descriptor count, and ownership
     * flag. A secondary constructor derives a sub-range by adding
     * index*increment to both handles. RTTI and sized deletion prove 0x48.
     */
    class CDescriptorHeap : public CDeviceObject {
      public:
        ~CDescriptorHeap() override = default;

        std::uint64_t GetCpuStart() const { return m_cpuStart; }
        std::uint64_t GetGpuStart() const { return m_gpuStart; }
        std::uint32_t GetHeapType() const { return m_heapType; }
        std::uint32_t GetHeapFlags() const { return m_heapFlags; }
        void *GetNativeHeap() const { return m_pNativeHeap; }
        std::uint32_t GetDescriptorIncrement() const {
            return m_descriptorIncrement;
        }
        std::uint32_t GetDescriptorCount() const { return m_descriptorCount; }
        bool OwnsNativeHeap() const { return m_ownsNativeHeap; }

      protected:
        std::uint64_t m_cpuStart;             // 0x18
        std::uint64_t m_gpuStart;             // 0x20
        std::uint32_t m_heapType;              // 0x28
        std::uint32_t m_heapFlags;             // 0x2C
        void *m_pNativeHeap;                   // 0x30
        std::uint32_t m_descriptorIncrement;   // 0x38
        std::uint32_t m_descriptorCount;       // 0x3C
        bool m_ownsNativeHeap;                 // 0x40
        std::byte m_padding0041[0x7];
    };

    /**
     * Device-owned D3D12 synchronization fence.
     *
     * RTTI proves the CDeviceObject base. Initialization creates and retains a
     * native D3D12 fence at +0x18 and a Win32 completion event at +0x20;
     * destruction closes the event before releasing the native fence. Sized
     * deletion proves the exact 0x28-byte extent.
     */
    class CFence : public CDeviceObject {
      public:
        ~CFence() override = default;

        void *GetNativeFence() const { return m_pNativeFence; }
        void *GetCompletionEvent() const { return m_completionEvent; }

      protected:
        void *m_pNativeFence;    // 0x18, retained ID3D12Fence
        void *m_completionEvent; // 0x20, HANDLE
    };

    /** ABI copy of DXGI_SWAP_CHAIN_DESC without a Windows SDK dependency. */
    struct SwapChainDescriptionLayout {
        std::uint32_t width;                  // 0x00
        std::uint32_t height;                 // 0x04
        std::uint32_t refreshRateNumerator;   // 0x08
        std::uint32_t refreshRateDenominator; // 0x0C
        std::uint32_t format;                 // 0x10, DXGI_FORMAT
        std::uint32_t scanlineOrdering;       // 0x14
        std::uint32_t scaling;                // 0x18
        std::uint32_t sampleCount;            // 0x1C
        std::uint32_t sampleQuality;          // 0x20
        std::uint32_t bufferUsage;            // 0x24
        std::uint32_t bufferCount;            // 0x28
        std::uint32_t padding002C;
        void *outputWindow;                   // 0x30, HWND
        std::int32_t windowed;                 // 0x38, BOOL
        std::uint32_t swapEffect;              // 0x3C
        std::uint32_t flags;                   // 0x40
        std::uint32_t padding0044;
    };

    /**
     * Native DXGI swap-chain wrapper and its presentation synchronization.
     *
     * RTTI proves CRefCounted as the sole base. Construction embeds CFence at
     * +0x20, copies an exact DXGI_SWAP_CHAIN_DESC to +0x58, retains the native
     * swap chain at +0xA8, and builds one intrusive CResource wrapper per back
     * buffer in the vector at +0xB0. Present advances the previous/current
     * presentation-fence values at +0x48/+0x50 before queueing the native
     * fence signal; buffer resize waits for +0x50. Sized deletion proves 0xC8.
     */
    class CSwapChain : public CRefCounted {
      public:
        ~CSwapChain() override = default;

        CommandQueueStateLayout *GetCommandQueueState() const {
            return m_pCommandQueueState;
        }
        const CFence &GetPresentationFence() const {
            return m_presentationFence;
        }
        std::uint64_t GetPreviousFenceValue() const {
            return m_previousFenceValue;
        }
        std::uint64_t GetCurrentFenceValue() const {
            return m_currentFenceValue;
        }
        const SwapChainDescriptionLayout &GetDescription() const {
            return m_description;
        }
        void *GetNativeSwapChain() const { return m_pNativeSwapChain; }
        CResource *const *GetBackBuffersBegin() const {
            return m_backBuffersBegin;
        }
        CResource *const *GetBackBuffersEnd() const {
            return m_backBuffersEnd;
        }
        std::size_t GetBackBufferCount() const {
            return m_backBuffersBegin
                       ? static_cast<std::size_t>(m_backBuffersEnd -
                                                  m_backBuffersBegin)
                       : 0;
        }

      protected:
        void *m_pQueueSynchronizationState;          // 0x10
        CommandQueueStateLayout *m_pCommandQueueState; // 0x18
        CFence m_presentationFence;                  // 0x20
        std::uint64_t m_previousFenceValue;          // 0x48
        std::uint64_t m_currentFenceValue;           // 0x50
        SwapChainDescriptionLayout m_description;   // 0x58
        std::uint64_t m_state00A0;
        void *m_pNativeSwapChain;                    // 0xA8
        CResource **m_backBuffersBegin;              // 0xB0
        CResource **m_backBuffersEnd;                // 0xB8
        CResource **m_backBuffersCapacity;           // 0xC0
    };

    /**
     * Native D3D12 recording command list and renderer-side state cache.
     *
     * RTTI proves CDeviceObject as the direct base. Queue-pool acquisition
     * allocates exactly 0x20B0 bytes and initializes the native device,
     * command-queue owner, command list, queried list extension, Aftermath
     * context, command allocator, and native queue in the leading 0x58 bytes.
     * The large state/cache body remains opaque until its independent tables
     * are proven. Destruction releases every retained native interface.
     */
    class CCommandList : public CDeviceObject {
      public:
        ~CCommandList() override = default;

        void *GetNativeDevice() const { return m_pNativeDevice; }
        CommandQueueStateLayout *GetCommandQueueState() const {
            return m_pCommandQueueState;
        }
        void *GetNativeCommandList() const { return m_pNativeCommandList; }
        void *GetNativeCommandListExtension() const {
            return m_pNativeCommandListExtension;
        }
        void *GetAftermathContext() const { return m_pAftermathContext; }
        std::uint32_t GetQueueType() const { return m_queueType; }
        void *GetNativeCommandAllocator() const {
            return m_pNativeCommandAllocator;
        }
        void *GetNativeCommandQueue() const { return m_pNativeCommandQueue; }

      protected:
        void *m_pNativeDevice;                         // 0x0018, raw
        CommandQueueStateLayout *m_pCommandQueueState; // 0x0020
        void *m_pNativeCommandList;                    // 0x0028
        void *m_pNativeCommandListExtension;           // 0x0030
        void *m_pAftermathContext;                     // 0x0038
        std::uint32_t m_queueType;                     // 0x0040
        std::uint32_t m_padding0044;
        void *m_pNativeCommandAllocator;               // 0x0048
        void *m_pNativeCommandQueue;                   // 0x0050
        std::byte m_state0058[0x18];
        std::uint64_t m_poolValue0070;
        void *m_pPoolState0078;
        bool m_state0080;
        bool m_state0081;
        std::byte m_padding0082[0x6];
        std::byte m_rendererState0088[0x2018];
        std::uint64_t m_state20A0;
        std::int32_t m_readyState20A8;
        std::uint32_t m_deviceState20AC;
    };

    /** Constructor/destructor-proven 0x28-byte MSVC deque storage. */
    struct DescriptorDequeLayout {
        void *map;             // 0x00
        std::uint64_t mapSize; // 0x08
        std::uint64_t offset;  // 0x10
        std::uint64_t size;    // 0x18
        std::uint64_t state;   // 0x20
    };

    /** MSVC deque storage used for 0x48-byte asynchronous command packets. */
    struct AsyncCommandDequeLayout {
        void *map;             // 0x00
        std::uint64_t mapSize; // 0x08
        std::uint64_t offset;  // 0x10
        std::uint64_t size;    // 0x18
        std::uint64_t state;   // 0x20
    };

    /**
     * Background command dispatcher embedded by each command-queue record.
     *
     * RTTI proves global IThread as the sole base. ThreadEntry waits on the
     * semaphore at +0x48, drains exact 0x48-byte command packets from the
     * SRW-locked deque at +0x18, and exits when +0x0C is set. The parent queue
     * record stores itself at +0x10 before registering the thread. Destruction
     * closes the semaphore and sized deletion proves the exact 0x58 bytes.
     */
    class CAsyncCommandQueue : public IThread {
      public:
        ~CAsyncCommandQueue() override = default;
        void ThreadEntry() override;

        CommandQueueStateLayout *GetCommandQueueState() const {
            return m_pCommandQueueState;
        }
        bool IsStopRequested() const { return m_stopRequested; }
        std::size_t GetQueuedCommandCount() const {
            return static_cast<std::size_t>(m_commands.size);
        }
        void *GetWorkSemaphore() const { return m_workSemaphore; }

      protected:
        std::int32_t m_outstandingWorkCount;       // 0x08
        bool m_stopRequested;                      // 0x0C
        bool m_state000D;
        std::byte m_padding000E[0x2];
        CommandQueueStateLayout *m_pCommandQueueState; // 0x10
        AsyncCommandDequeLayout m_commands;        // 0x18
        void *m_srwLock;                           // 0x40
        void *m_workSemaphore;                     // 0x48, HANDLE
        std::int32_t m_semaphoreCount;              // 0x50
        std::uint32_t m_padding0054;
    };

    /** One owned CPU-descriptor heap/allocation record (exact stride). */
    struct CPUDescriptorHeapRecordLayout {
        std::byte data[0x50];
    };

    /**
     * Per-heap-type CPU descriptor allocator owned by CDevice.
     *
     * RTTI proves CDeviceObject as the direct base, and sized deletion proves
     * the exact 0x110-byte extent. The device constructor embeds four
     * consecutive instances at +0x160/+0x270/+0x380/+0x490. Destruction walks
     * the 0x50-stride heap-record vector, destroys the primary deque and four
     * additional descriptor deques, then releases the device-object base.
     */
    class CCPUDescriptorManager : public CDeviceObject {
      public:
        ~CCPUDescriptorManager() override = default;

        const CPUDescriptorHeapRecordLayout *GetHeapRecordsBegin() const {
            return m_heapRecordsBegin;
        }
        const CPUDescriptorHeapRecordLayout *GetHeapRecordsEnd() const {
            return m_heapRecordsEnd;
        }
        std::size_t GetHeapRecordCount() const {
            return m_heapRecordsBegin
                       ? static_cast<std::size_t>(m_heapRecordsEnd -
                                                  m_heapRecordsBegin)
                       : 0;
        }
        const DescriptorDequeLayout &GetPendingDescriptors() const {
            return m_pendingDescriptors;
        }
        const DescriptorDequeLayout *GetDescriptorQueue(
            std::size_t index) const {
            return index < 4 ? &m_descriptorQueues[index] : nullptr;
        }

      protected:
        CPUDescriptorHeapRecordLayout *m_heapRecordsBegin;    // 0x18
        CPUDescriptorHeapRecordLayout *m_heapRecordsEnd;      // 0x20
        CPUDescriptorHeapRecordLayout *m_heapRecordsCapacity; // 0x28
        DescriptorDequeLayout m_pendingDescriptors;           // 0x30
        DescriptorDequeLayout m_descriptorQueues[4];          // 0x58
        void *m_srwLock;                                      // 0xF8
        std::uint64_t m_state0100;
        std::uint32_t m_state0108;
        std::uint32_t m_padding010C;
    };

    /**
     * Per-device D3D12 descriptor allocation manager.
     *
     * CDevice embeds two instances at +0x5A0 and +0x888. Each manager owns
     * one leading deque and two banks of eight 0x28-byte deque records. The
     * constructor initializes all sixteen bank entries independently and the
     * destructor destroys both arrays before releasing the device-object
     * base. RTTI and sized deletion prove the exact 0x2E8-byte extent.
     */
    class CDeviceDescriptorManager : public CDeviceObject {
      public:
        ~CDeviceDescriptorManager() override = default;

        const DescriptorDequeLayout &GetPendingDescriptors() const {
            return m_pendingDescriptors;
        }

        const DescriptorDequeLayout *GetDescriptorBank(std::size_t bank) const {
            return bank < 2 ? m_descriptorBanks[bank] : nullptr;
        }

      private:
        void *m_state0018;                            // 0x18
        DescriptorDequeLayout m_pendingDescriptors;   // 0x20
        DescriptorDequeLayout m_descriptorBanks[2][8]; // 0x48
        std::uint64_t m_state02C8;
        std::uint64_t m_state02D0;
        void *m_srwLock;                              // 0x2D8
        std::uint64_t m_state02E0;
    };

    /** One of the scheduler's three constructor-indexed command queues. */
    struct CommandQueueStateLayout {
        CDevice *device;                         // 0x00
        void *scheduler;                         // 0x08, CCommandScheduler *
        void *sharedState;                       // 0x10
        std::uint32_t queueType;                 // 0x18, 0/1/2
        std::uint32_t padding001C;
        DescriptorDequeLayout pending[3];        // 0x20
        void *srwLock;                           // 0x98
        std::uint64_t state00A0;
        void *nativeQueue;                       // 0xA8, retained object
        CAsyncCommandQueue asyncCommandQueue;    // 0xB0
        std::uint32_t deviceState0108;           // 0x108
        std::uint32_t padding010C;
    };

    /**
     * D3D12 graphics/compute/copy command scheduler owned by CDevice.
     *
     * RTTI proves CDeviceObject as its direct base. Construction creates one
     * shared 0xC8 synchronization block at +0x620, followed by three exact
     * 0x110-byte queue records at +0x6E8 with type indices 0, 1, and 2. The
     * deleting destructor proves the complete 0xAB8-byte extent.
     */
    class CCommandScheduler : public CDeviceObject {
      public:
        ~CCommandScheduler() override = default;

        const CommandQueueStateLayout *GetCommandQueue(
            std::size_t queueType) const {
            return queueType < 3 ? &m_commandQueues[queueType] : nullptr;
        }

        CommandQueueStateLayout *GetCommandQueue(std::size_t queueType) {
            return queueType < 3 ? &m_commandQueues[queueType] : nullptr;
        }

      private:
        std::byte m_state0018[0x600];
        std::uint32_t m_state0618;
        std::uint32_t m_padding061C;
        std::byte m_synchronizationState0620[0xC8];
        CommandQueueStateLayout m_commandQueues[3]; // 0x6E8
        std::byte m_cacheState0A18[0xA0];
    };

    /**
     * Concrete CryRenderer D3D12 device.
     *
     * RTTI proves CRefCounted as its sole base and the scalar deleting
     * destructor proves the exact 0x1800-byte extent. The default constructor
     * builds two complete descriptor managers at +0x5A0 and +0x888. Device
     * initialization queries the created D3D12 device for ID3D12Device3 and
     * retains that interface at +0x10. Its GetNodeCount result is cached at
     * +0x5C and expanded into an all-nodes mask at +0x68. Other native COM
     * owners and caches remain opaque until their contracts are proven.
     */
    class CDevice : public CRefCounted {
      public:
        ~CDevice() override = default;

        void *GetNativeDevice3() const { return m_pNativeDevice3; }
        std::uint32_t GetNodeCount() const { return m_nodeCount; }
        std::uint32_t GetAllNodeMask() const { return m_allNodeMask; }

        CDeviceDescriptorManager *GetDescriptorManager(std::size_t index) {
            return index < 2 ? &m_descriptorManagers[index] : nullptr;
        }

        const CDeviceDescriptorManager *GetDescriptorManager(
            std::size_t index) const {
            return index < 2 ? &m_descriptorManagers[index] : nullptr;
        }

        CCPUDescriptorManager *GetCpuDescriptorManager(std::size_t index) {
            return index < 4 ? &m_cpuDescriptorManagers[index] : nullptr;
        }

        const CCPUDescriptorManager *GetCpuDescriptorManager(
            std::size_t index) const {
            return index < 4 ? &m_cpuDescriptorManagers[index] : nullptr;
        }

        CQueryHeap *GetQueryHeap(std::size_t index) {
            return index < 2 ? &m_queryHeaps[index] : nullptr;
        }

        const CQueryHeap *GetQueryHeap(std::size_t index) const {
            return index < 2 ? &m_queryHeaps[index] : nullptr;
        }

        CCommandScheduler *GetCommandScheduler() {
            return &m_commandScheduler;
        }

        const CCommandScheduler *GetCommandScheduler() const {
            return &m_commandScheduler;
        }

      private:
        void *m_pNativeDevice3;             // 0x10, retained ID3D12Device3
        void *m_pNativeInterface0018;       // 0x18, retained COM interface
        bool m_state0020;                   // 0x20
        std::byte m_padding0021[0x7];       // 0x21
        std::byte m_state0028[0x34];        // 0x28
        std::uint32_t m_nodeCount;          // 0x5C
        std::uint32_t m_state0060;          // 0x60
        std::uint32_t m_state0064;          // 0x64
        std::uint32_t m_allNodeMask;        // 0x68
        std::uint32_t m_padding006C;        // 0x6C
        std::byte m_state0070[0x48];        // 0x70
        CQueryHeap m_queryHeaps[2]; // 0xB8
        std::byte m_state0118[0x48];
        CCPUDescriptorManager m_cpuDescriptorManagers[4]; // 0x160
        CDeviceDescriptorManager m_descriptorManagers[2]; // 0x5A0
        std::byte m_state0B70[0x118];
        CCommandScheduler m_commandScheduler; // 0xC88
        std::byte m_state1740[0xC0];
    };

    static_assert(sizeof(CRefCounted) == 0x10,
                  "NCryDX12::CRefCounted size mismatch");
    static_assert(sizeof(CDeviceObject) == 0x18,
                  "NCryDX12::CDeviceObject size mismatch");
    static_assert(sizeof(CQueryHeap) == 0x30,
                  "NCryDX12::CQueryHeap size mismatch");
    static_assert(sizeof(QueryHeapDescriptionLayout) == 0x10,
                  "NCryDX12::QueryHeapDescriptionLayout size mismatch");
    static_assert(sizeof(SamplerDescriptionLayout) == 0x34,
                  "NCryDX12::SamplerDescriptionLayout size mismatch");
    static_assert(sizeof(CSamplerState) == 0x58,
                  "NCryDX12::CSamplerState size mismatch");
    static_assert(sizeof(CShader) == 0x30,
                  "NCryDX12::CShader size mismatch");
    static_assert(sizeof(InputElementDescriptionLayout) == 0x20,
                  "NCryDX12::InputElementDescriptionLayout size mismatch");
    static_assert(sizeof(CInputLayout) == 0x40,
                  "NCryDX12::CInputLayout size mismatch");
    static_assert(sizeof(DescriptorRangeLayout) == 0x14,
                  "NCryDX12::DescriptorRangeLayout size mismatch");
    static_assert(sizeof(RootDescriptorTableLayout) == 0x10,
                  "NCryDX12::RootDescriptorTableLayout size mismatch");
    static_assert(sizeof(RootConstantsLayout) == 0x0C,
                  "NCryDX12::RootConstantsLayout size mismatch");
    static_assert(sizeof(RootDescriptorLayout) == 0x08,
                  "NCryDX12::RootDescriptorLayout size mismatch");
    static_assert(sizeof(RootParameterLayout) == 0x20,
                  "NCryDX12::RootParameterLayout size mismatch");
    static_assert(sizeof(StaticSamplerDescriptionLayout) == 0x34,
                  "NCryDX12::StaticSamplerDescriptionLayout size mismatch");
    static_assert(sizeof(RootSignatureDescriptionLayout) == 0x1558,
                  "NCryDX12::RootSignatureDescriptionLayout size mismatch");
    static_assert(sizeof(CRootSignature) == 0x1580,
                  "NCryDX12::CRootSignature size mismatch");
    static_assert(sizeof(ShaderBytecodeLayout) == 0x10,
                  "NCryDX12::ShaderBytecodeLayout size mismatch");
    static_assert(sizeof(CachedPipelineStateLayout) == 0x10,
                  "NCryDX12::CachedPipelineStateLayout size mismatch");
    static_assert(sizeof(ComputePipelineStateDescriptionLayout) == 0x38,
                  "NCryDX12::ComputePipelineStateDescriptionLayout size mismatch");
    static_assert(sizeof(CPSO) == 0x28,
                  "NCryDX12::CPSO size mismatch");
    static_assert(sizeof(CComputePSO) == 0x60,
                  "NCryDX12::CComputePSO size mismatch");
    static_assert(sizeof(GraphicsPipelineStateDescriptionLayout) == 0x290,
                  "NCryDX12::GraphicsPipelineStateDescriptionLayout size mismatch");
    static_assert(sizeof(CGraphicsPSO) == 0x2B8,
                  "NCryDX12::CGraphicsPSO size mismatch");
    static_assert(sizeof(ResourceDescriptionLayout) == 0x38,
                  "NCryDX12::ResourceDescriptionLayout size mismatch");
    static_assert(sizeof(CResource) == 0xB8,
                  "NCryDX12::CResource size mismatch");
    static_assert(sizeof(CView) == 0x50,
                  "NCryDX12::CView size mismatch");
    static_assert(sizeof(CDescriptorHeap) == 0x48,
                  "NCryDX12::CDescriptorHeap size mismatch");
    static_assert(sizeof(CFence) == 0x28,
                  "NCryDX12::CFence size mismatch");
    static_assert(sizeof(SwapChainDescriptionLayout) == 0x48,
                  "NCryDX12::SwapChainDescriptionLayout size mismatch");
    static_assert(sizeof(CSwapChain) == 0xC8,
                  "NCryDX12::CSwapChain size mismatch");
    static_assert(sizeof(CCommandList) == 0x20B0,
                  "NCryDX12::CCommandList size mismatch");
    static_assert(sizeof(DescriptorDequeLayout) == 0x28,
                  "NCryDX12::DescriptorDequeLayout size mismatch");
    static_assert(sizeof(IThread) == 0x08,
                  "IThread size mismatch");
    static_assert(sizeof(AsyncCommandDequeLayout) == 0x28,
                  "NCryDX12::AsyncCommandDequeLayout size mismatch");
    static_assert(sizeof(CAsyncCommandQueue) == 0x58,
                  "NCryDX12::CAsyncCommandQueue size mismatch");
    static_assert(sizeof(CPUDescriptorHeapRecordLayout) == 0x50,
                  "NCryDX12::CPUDescriptorHeapRecordLayout size mismatch");
    static_assert(sizeof(CCPUDescriptorManager) == 0x110,
                  "NCryDX12::CCPUDescriptorManager size mismatch");
    static_assert(sizeof(CDeviceDescriptorManager) == 0x2E8,
                  "NCryDX12::CDeviceDescriptorManager size mismatch");
    static_assert(sizeof(CommandQueueStateLayout) == 0x110,
                  "NCryDX12::CommandQueueStateLayout size mismatch");
    static_assert(sizeof(CCommandScheduler) == 0xAB8,
                  "NCryDX12::CCommandScheduler size mismatch");
    static_assert(sizeof(CDevice) == 0x1800,
                  "NCryDX12::CDevice size mismatch");
} // namespace SDK::NCryDX12
