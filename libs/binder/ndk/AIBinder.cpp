/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android/binder_ibinder.h>
#include "AIBinder_internal.h"

#include <android/binder_status.h>
#include "AParcel_internal.h"

#include <android-base/logging.h>

using ::android::IBinder;
using ::android::Parcel;
using ::android::sp;
using ::android::String16;
using ::android::wp;

AIBinder::AIBinder(const AIBinder_Class* clazz) : mClazz(clazz) {}
AIBinder::~AIBinder() {}

sp<AIBinder> AIBinder::associateClass(const AIBinder_Class* clazz) {
    using ::android::String8;

    if (clazz == nullptr) return nullptr;
    if (mClazz == clazz) return this;

    String8 newDescriptor(clazz->getInterfaceDescriptor());

    if (mClazz != nullptr) {
        String8 currentDescriptor(mClazz->getInterfaceDescriptor());
        if (newDescriptor == currentDescriptor) {
            LOG(ERROR) << __func__ << ": Class descriptors '" << currentDescriptor
                       << "' match during associateClass, but they are different class objects. "
                          "Class descriptor collision?";
            return nullptr;
        }

        LOG(ERROR) << __func__
                   << ": Class cannot be associated on object which already has a class. Trying to "
                      "associate to '"
                   << newDescriptor.c_str() << "' but already set to '" << currentDescriptor.c_str()
                   << "'.";
        return nullptr;
    }

    String8 descriptor(getBinder()->getInterfaceDescriptor());
    if (descriptor != newDescriptor) {
        LOG(ERROR) << __func__ << ": Expecting binder to have class '" << newDescriptor.c_str()
                   << "' but descriptor is actually '" << descriptor.c_str() << "'.";
        return nullptr;
    }

    // The descriptor matches, so if it is local, this is guaranteed to be the libbinder_ndk class.
    // An error here can occur if there is a conflict between descriptors (two unrelated classes
    // define the same descriptor), but this should never happen.

    // if this is a local ABBinder, mClazz should be non-null
    CHECK(asABBinder() == nullptr);
    CHECK(asABpBinder() != nullptr);

    if (!isRemote()) {
        // ABpBinder but proxy to a local object. Therefore that local object must be an ABBinder.
        ABBinder* binder = static_cast<ABBinder*>(getBinder().get());
        return binder;
    }

    // This is a remote object
    mClazz = clazz;

    return this;
}

ABBinder::ABBinder(const AIBinder_Class* clazz, void* userData)
      : AIBinder(clazz), BBinder(), mUserData(userData) {
    CHECK(clazz != nullptr);
}
ABBinder::~ABBinder() {
    getClass()->onDestroy(mUserData);
}

const String16& ABBinder::getInterfaceDescriptor() const {
    return getClass()->getInterfaceDescriptor();
}

binder_status_t ABBinder::onTransact(transaction_code_t code, const Parcel& data, Parcel* reply,
                                     binder_flags_t flags) {
    if (isUserCommand(code)) {
        if (!data.checkInterface(this)) {
            return EX_ILLEGAL_STATE;
        }

        const AParcel in = AParcel::readOnly(this, &data);
        AParcel out = AParcel(this, reply, false /*owns*/);

        return getClass()->onTransact(this, code, &in, &out);
    } else {
        return BBinder::onTransact(code, data, reply, flags);
    }
}

ABpBinder::ABpBinder(::android::sp<::android::IBinder> binder)
      : AIBinder(nullptr /*clazz*/), BpRefBase(binder) {
    CHECK(binder != nullptr);
}
ABpBinder::~ABpBinder() {}

struct AIBinder_Weak {
    wp<AIBinder> binder;
};
AIBinder_Weak* AIBinder_Weak_new(AIBinder* binder) {
    if (binder == nullptr) return nullptr;
    return new AIBinder_Weak{wp<AIBinder>(binder)};
}
void AIBinder_Weak_delete(AIBinder_Weak* weakBinder) {
    delete weakBinder;
}
AIBinder* AIBinder_Weak_promote(AIBinder_Weak* weakBinder) {
    if (weakBinder == nullptr) return nullptr;
    sp<AIBinder> binder = weakBinder->binder.promote();
    AIBinder_incStrong(binder.get());
    return binder.get();
}

AIBinder_Class::AIBinder_Class(const char* interfaceDescriptor, AIBinder_Class_onCreate onCreate,
                               AIBinder_Class_onDestroy onDestroy,
                               AIBinder_Class_onTransact onTransact)
      : onCreate(onCreate),
        onDestroy(onDestroy),
        onTransact(onTransact),
        mInterfaceDescriptor(interfaceDescriptor) {}

AIBinder_Class* AIBinder_Class_define(const char* interfaceDescriptor,
                                      AIBinder_Class_onCreate onCreate,
                                      AIBinder_Class_onDestroy onDestroy,
                                      AIBinder_Class_onTransact onTransact) {
    if (interfaceDescriptor == nullptr || onCreate == nullptr || onDestroy == nullptr ||
        onTransact == nullptr) {
        return nullptr;
    }

    return new AIBinder_Class(interfaceDescriptor, onCreate, onDestroy, onTransact);
}

AIBinder* AIBinder_new(const AIBinder_Class* clazz, void* args) {
    if (clazz == nullptr) {
        LOG(ERROR) << __func__ << ": Must provide class to construct local binder.";
        return nullptr;
    }

    void* userData = clazz->onCreate(args);

    AIBinder* ret = new ABBinder(clazz, userData);
    AIBinder_incStrong(ret);
    return ret;
}

bool AIBinder_isRemote(AIBinder* binder) {
    if (binder == nullptr) {
        return true;
    }

    return binder->isRemote();
}

void AIBinder_incStrong(AIBinder* binder) {
    if (binder == nullptr) {
        LOG(ERROR) << __func__ << ": on null binder";
        return;
    }

    binder->incStrong(nullptr);
}
void AIBinder_decStrong(AIBinder* binder) {
    if (binder == nullptr) {
        LOG(ERROR) << __func__ << ": on null binder";
        return;
    }

    binder->decStrong(nullptr);
}
int32_t AIBinder_debugGetRefCount(AIBinder* binder) {
    if (binder == nullptr) {
        LOG(ERROR) << __func__ << ": on null binder";
        return -1;
    }

    return binder->getStrongCount();
}

void AIBinder_associateClass(AIBinder** binder, const AIBinder_Class* clazz) {
    if (binder == nullptr || *binder == nullptr) {
        return;
    }

    sp<AIBinder> result = (*binder)->associateClass(clazz);

    // This function takes one refcount of 'binder' and delivers one refcount of 'result' to the
    // callee. First we give the callee their refcount and then take it away from binder. This is
    // done in this order in order to handle the case that the result and the binder are the same
    // object.
    if (result != nullptr) {
        AIBinder_incStrong(result.get());
    }
    AIBinder_decStrong(*binder);

    *binder = result.get(); // Maybe no-op
}

const AIBinder_Class* AIBinder_getClass(AIBinder* binder) {
    if (binder == nullptr) {
        return nullptr;
    }

    return binder->getClass();
}

void* AIBinder_getUserData(AIBinder* binder) {
    if (binder == nullptr) {
        return nullptr;
    }

    ABBinder* bBinder = binder->asABBinder();
    if (bBinder == nullptr) {
        return nullptr;
    }

    return bBinder->getUserData();
}

binder_status_t AIBinder_prepareTransaction(AIBinder* binder, AParcel** in) {
    if (binder == nullptr || in == nullptr) {
        LOG(ERROR) << __func__ << ": requires non-null parameters.";
        return EX_NULL_POINTER;
    }
    const AIBinder_Class* clazz = binder->getClass();
    if (clazz == nullptr) {
        LOG(ERROR) << __func__
                   << ": Class must be defined for a remote binder transaction. See "
                      "AIBinder_associateClass.";
        return EX_ILLEGAL_STATE;
    }

    *in = new AParcel(binder);
    binder_status_t status = (**in)->writeInterfaceToken(clazz->getInterfaceDescriptor());
    if (status != EX_NONE) {
        delete *in;
        *in = nullptr;
    }

    return status;
}

using AutoParcelDestroyer = std::unique_ptr<AParcel*, void (*)(AParcel**)>;
static void destroy_parcel(AParcel** parcel) {
    delete *parcel;
    *parcel = nullptr;
}

binder_status_t AIBinder_transact(AIBinder* binder, transaction_code_t code, AParcel** in,
                                  AParcel** out, binder_flags_t flags) {
    if (in == nullptr) {
        LOG(ERROR) << __func__ << ": requires non-null in parameter";
        return EX_NULL_POINTER;
    }

    // This object is the input to the transaction. This function takes ownership of it and deletes
    // it.
    AutoParcelDestroyer forIn(in, destroy_parcel);

    if (!isUserCommand(code)) {
        LOG(ERROR) << __func__ << ": Only user-defined transactions can be made from the NDK.";
        return EX_UNSUPPORTED_OPERATION;
    }

    if ((flags & ~FLAG_ONEWAY) != 0) {
        LOG(ERROR) << __func__ << ": Unrecognized flags sent: " << flags;
        return EX_ILLEGAL_ARGUMENT;
    }

    if (binder == nullptr || *in == nullptr || out == nullptr) {
        LOG(ERROR) << __func__ << ": requires non-null parameters.";
        return EX_NULL_POINTER;
    }

    if ((*in)->getBinder() != binder) {
        LOG(ERROR) << __func__ << ": parcel is associated with binder object " << binder
                   << " but called with " << (*in)->getBinder();
        return EX_ILLEGAL_STATE;
    }

    *out = new AParcel(binder);

    binder_status_t parcelStatus =
            binder->getBinder()->transact(code, *(*in)->operator->(), (*out)->operator->(), flags);

    if (parcelStatus != EX_NONE) {
        delete *out;
        *out = nullptr;
    }

    return parcelStatus;
}

binder_status_t AIBinder_finalizeTransaction(AIBinder* binder, AParcel** out) {
    if (out == nullptr) {
        LOG(ERROR) << __func__ << ": requires non-null out parameter";
        return EX_NULL_POINTER;
    }

    // This object is the input to the transaction. This function takes ownership of it and deletes
    // it.
    AutoParcelDestroyer forOut(out, destroy_parcel);

    if (binder == nullptr || *out == nullptr) {
        LOG(ERROR) << __func__ << ": requires non-null parameters.";
        return EX_NULL_POINTER;
    }

    if ((*out)->getBinder() != binder) {
        LOG(ERROR) << __func__ << ": parcel is associated with binder object " << binder
                   << " but called with " << (*out)->getBinder();
        return EX_ILLEGAL_STATE;
    }

    if ((**out)->dataAvail() != 0) {
        LOG(ERROR) << __func__
                   << ": Only part of this transaction was read. There is remaining data left.";
        return EX_ILLEGAL_STATE;
    }

    return EX_NONE;
}
