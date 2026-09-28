#pragma once

#if defined(__APPLE__)

#include "Allocator.hpp"
#include "../misc/Attachment.hpp"
#include <IOSurface/IOSurfaceRef.h>

namespace Aquamarine {
    class CIOSurfaceAllocator;
    class CBackend;
    class CSwapchain;

    class CIOSurfaceAttachment : public IAttachment {
      public:
        CIOSurfaceAttachment(IOSurfaceRef surface_);
        virtual ~CIOSurfaceAttachment();

        IOSurfaceRef surface = nullptr;
    };

    class CIOSurfaceBuffer : public IBuffer {
      public:
        virtual ~CIOSurfaceBuffer();

        virtual eBufferCapability                      caps();
        virtual eBufferType                            type();
        virtual void                                   update(const Hyprutils::Math::CRegion& damage);
        virtual bool                                   isSynchronous();
        virtual bool                                   good();
        virtual SDMABUFAttrs                           dmabuf();
        virtual std::tuple<uint8_t*, uint32_t, size_t> beginDataPtr(uint32_t flags);
        virtual void                                   endDataPtr();

      private:
        CIOSurfaceBuffer(const SAllocatorBufferParams& params, Hyprutils::Memory::CWeakPointer<CIOSurfaceAllocator> allocator_,
                         Hyprutils::Memory::CSharedPointer<CSwapchain> swapchain);

        Hyprutils::Memory::CWeakPointer<CIOSurfaceAllocator> allocator;

        IOSurfaceRef surface = nullptr;
        uint8_t*     mapping = nullptr;
        SDMABUFAttrs attrs{.success = false};

        friend class CIOSurfaceAllocator;
    };

    class CIOSurfaceAllocator : public IAllocator {
      public:
        ~CIOSurfaceAllocator();
        static Hyprutils::Memory::CSharedPointer<CIOSurfaceAllocator> create(Hyprutils::Memory::CWeakPointer<CBackend> backend_);

        virtual Hyprutils::Memory::CSharedPointer<IBuffer>            acquire(const SAllocatorBufferParams& params, Hyprutils::Memory::CSharedPointer<CSwapchain> swapchain_);
        virtual Hyprutils::Memory::CSharedPointer<CBackend>           getBackend();
        virtual int                                                   drmFD();
        virtual eAllocatorType                                        type();
        virtual void                                                  destroyBuffers();

        Hyprutils::Memory::CWeakPointer<CIOSurfaceAllocator> self;

      private:
        CIOSurfaceAllocator(Hyprutils::Memory::CWeakPointer<CBackend> backend_);

        std::vector<Hyprutils::Memory::CWeakPointer<CIOSurfaceBuffer>> buffers;

        Hyprutils::Memory::CWeakPointer<CBackend>                      backend;

        friend class CIOSurfaceBuffer;
    };
};

#endif
