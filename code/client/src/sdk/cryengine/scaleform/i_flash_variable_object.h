#pragma once

#include "../../types.h"
#include "s_flash_cxform.h"
#include "s_flash_display_info.h"
#include "s_flash_var_value.h"

namespace SDK {
    /**
     * Game table 0x183A55BA0, slots 0..46. Distinct names preserve overload
     * order in MSVC. Every returned handle is released through Release().
     * Clone shares the AS value; it does not deep-copy the display tree.
     * Scalar strings are borrowed from this handle's last-result cache;
     * copy them before another result-producing call or releasing the handle.
     */
    struct IFlashVariableObject {
        struct ObjectVisitor {
            virtual void Visit(const char *name) = 0;

          protected:
            virtual ~ObjectVisitor() = default;
        };

        virtual void Release()                                                              = 0; // 0
        virtual IFlashVariableObject *Clone() const                                         = 0;
        virtual bool IsObject() const                                                       = 0;
        virtual bool IsArray() const                                                        = 0;
        virtual bool IsDisplayObject() const                                                = 0;
        virtual SFlashVarValue ToVarValue() const                                           = 0; // 5, member return buffer
        virtual bool HasMember(const char *name) const                                      = 0;
        virtual bool SetMemberObject(const char *name, const IFlashVariableObject *object)  = 0;
        virtual bool SetMember(const char *name, const SFlashVarValue &value)               = 0;
        virtual bool GetMemberObject(const char *name, IFlashVariableObject *&object) const = 0;
        virtual bool GetMember(const char *name, SFlashVarValue &value) const               = 0; // 10
        virtual void VisitMembers(ObjectVisitor *visitor) const                             = 0;
        virtual bool DeleteMember(const char *name)                                         = 0;
        /** Native object overload rejects a null argument list even at count 0. */
        virtual bool InvokeObjects(const char *name, const IFlashVariableObject **arguments, unsigned int count, SFlashVarValue *result) = 0;
        virtual bool Invoke(const char *name, const SFlashVarValue *arguments, unsigned int count, SFlashVarValue *result)               = 0;
        virtual unsigned int GetArraySize() const                                                                                        = 0; // 15
        virtual bool SetArraySize(unsigned int size)                                                                                     = 0;
        virtual bool SetElementObject(unsigned int index, const IFlashVariableObject *object)                                            = 0;
        virtual bool SetElement(unsigned int index, const SFlashVarValue &value)                                                         = 0;
        virtual bool GetElementObject(unsigned int index, IFlashVariableObject *&object) const                                           = 0;
        virtual bool GetElement(unsigned int index, SFlashVarValue &value) const                                                         = 0; // 20
        virtual bool PushBackObject(const IFlashVariableObject *object)                                                                  = 0;
        virtual bool PushBack(const SFlashVarValue &value)                                                                               = 0;
        virtual bool PopBack()                                                                                                           = 0;
        virtual bool RemoveElements(unsigned int index, int count = -1)                                                                  = 0;
        /** Scale and alpha use percentages; presence bits select changed fields. */
        virtual bool SetDisplayInfo(const SFlashDisplayInfo &info) = 0; // 25
        virtual bool GetDisplayInfo(SFlashDisplayInfo &info) const = 0;
        /** Affine 2D uses the first two rows; Get fills final row with 0,0,1. */
        virtual bool SetDisplayMatrix(const Matrix33 &matrix)                                                                                                                     = 0;
        virtual bool GetDisplayMatrix(Matrix33 &matrix) const                                                                                                                     = 0;
        virtual bool Set3DMatrix(const Matrix44 &matrix)                                                                                                                          = 0;
        virtual bool Get3DMatrix(Matrix44 &matrix) const                                                                                                                          = 0; // 30
        virtual bool SetColorTransform(const SFlashCxform &transform)                                                                                                             = 0;
        virtual bool GetColorTransform(SFlashCxform &transform) const                                                                                                             = 0;
        virtual bool SetVisible(bool visible)                                                                                                                                     = 0;
        virtual bool SetTextWide(const wchar_t *text)                                                                                                                             = 0;
        virtual bool SetText(const char *text)                                                                                                                                    = 0; // 35
        virtual bool SetTextHTMLWide(const wchar_t *text)                                                                                                                         = 0;
        virtual bool SetTextHTML(const char *text)                                                                                                                                = 0;
        virtual bool GetText(SFlashVarValue &text) const                                                                                                                          = 0;
        virtual bool GetTextHTML(SFlashVarValue &text) const                                                                                                                      = 0;
        virtual bool CreateEmptyMovieClip(IFlashVariableObject *&clip, const char *instanceName, int depth = -1)                                                                  = 0; // 40
        virtual bool AttachMovie(IFlashVariableObject *&clip, const char *symbolName, const char *instanceName, int depth = -1, const IFlashVariableObject *initObject = nullptr) = 0;
        /** Numeric frames are one-based. Labels use separate virtual slots. */
        virtual bool GotoAndPlayFrame(unsigned int frame) = 0;
        virtual bool GotoAndPlayLabel(const char *label)  = 0;
        virtual bool GotoAndStopFrame(unsigned int frame) = 0;
        virtual bool GotoAndStopLabel(const char *label)  = 0; // 45

      protected:
        virtual ~IFlashVariableObject() = default; // 46; use Release()
    };
    static_assert(sizeof(IFlashVariableObject) == 8, "IFlashVariableObject size mismatch");
} // namespace SDK
