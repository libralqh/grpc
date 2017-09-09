/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "call_credentials.h"
#include "call.h"
#include "channel_credentials.h"
#include "common.h"

#include "hphp/runtime/base/array-init.h"
#include "hphp/runtime/base/builtin-functions.h"
#include "hphp/runtime/vm/native-data.h"

#include "grpc/support/alloc.h"
#include "grpc/support/string_util.h"

namespace HPHP {

/*****************************************************************************/
/*                       Crendentials Plugin Functions                       */
/*****************************************************************************/

typedef struct plugin_state
{
    Variant callback;
} plugin_state;

// forward declarations
int plugin_get_metadata(
    void *ptr, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t *num_creds_md, grpc_status_code *status,
    const char **error_details);
void plugin_destroy_state(void *ptr);


/*****************************************************************************/
/*                           Call Credentials Data                           */
/*****************************************************************************/

Class* CallCredentialsData::s_pClass{ nullptr };
const StaticString CallCredentialsData::s_ClassName{ "Grpc\\CallCredentials" };

Class* const CallCredentialsData::getClass(void)
{
    if (!s_pClass)
    {
        s_pClass = Unit::lookupClass(s_ClassName.get());
        assert(s_pClass);
    }
    return s_pClass;
}

CallCredentialsData::CallCredentialsData(void) : m_pCallCredentials{ nullptr }
{
}

CallCredentialsData::~CallCredentialsData(void)
{
    destroy();
}

void CallCredentialsData::init(grpc_call_credentials* const pCallCredentials)
{
    // destroy any existing call credetials
    destroy();

    // take ownership of new call credentials
    m_pCallCredentials = pCallCredentials;
}

void CallCredentialsData::destroy(void)
{
    if (m_pCallCredentials)
    {
        grpc_call_credentials_release(m_pCallCredentials);
        m_pCallCredentials = nullptr;
    }
}

/*****************************************************************************/
/*                        HHVM Call Credentials Methods                      */
/*****************************************************************************/

/**
 * Create composite credentials from two existing credentials.
 * @param CallCredentials $cred1_obj The first credential
 * @param CallCredentials $cred2_obj The second credential
 * @return CallCredentials The new composite credentials object
 */
Object HHVM_STATIC_METHOD(CallCredentials, createComposite,
                          const Object& cred1_obj,
                          const Object& cred2_obj)
{
    HHVM_TRACE_SCOPE("CallCredentials createComposite") // Degug Trace

    CallCredentialsData* const pCallCredentialsData1{ Native::data<CallCredentialsData>(cred1_obj) };
    CallCredentialsData* const pCallCredentialsData2{ Native::data<CallCredentialsData>(cred2_obj) };

    grpc_call_credentials* const pCallCredentials{
        grpc_composite_call_credentials_create(pCallCredentialsData1->credentials(),
                                               pCallCredentialsData2->credentials(),
                                               nullptr) };

    if (!pCallCredentials)
    {
        SystemLib::throwBadMethodCallExceptionObject("Failed to create call credentials composite");
    }

    Object newCallCredentialsObj{ CallCredentialsData::getClass() };
    CallCredentialsData* const pNewCallCredentialsData{ Native::data<CallCredentialsData>(newCallCredentialsObj)};
    pNewCallCredentialsData->init(pCallCredentials);

    return newCallCredentialsObj;
}

/**
 * Create a call credentials object from the plugin API
 * @param callable $fci The callback
 * @return CallCredentials The new call credentials object
 */
Object HHVM_STATIC_METHOD(CallCredentials, createFromPlugin,
                          const Variant& callback)
{
    HHVM_TRACE_SCOPE("CallCredentials createFromPlugin") // Degug Trace

    if (callback.isNull() || !is_callable(callback))
    {
        SystemLib::throwInvalidArgumentExceptionObject("Callback argument is not a valid callback");
    }

    Object newCallCredentialsObj{ CallCredentialsData::getClass() };
    CallCredentialsData* const pNewCallCredentialsData{ Native::data<CallCredentialsData>(newCallCredentialsObj) };

    plugin_state *pState{ reinterpret_cast<plugin_state*>(gpr_zalloc(sizeof(plugin_state))) };
    pState->callback = callback;

    grpc_metadata_credentials_plugin plugin;
    plugin.get_metadata = plugin_get_metadata;
    plugin.destroy = plugin_destroy_state;
    plugin.state = reinterpret_cast<void *>(pState);
    plugin.type = "";

    grpc_call_credentials* pCallCredentials{ grpc_metadata_credentials_create_from_plugin(plugin, nullptr) };

    if (!pCallCredentials)
    {
        SystemLib::throwBadMethodCallExceptionObject("failed to create call credntials plugin");
    }
    pNewCallCredentialsData->init(pCallCredentials);

    return newCallCredentialsObj;
}

/*****************************************************************************/
/*                       Crendentials Plugin Functions                       */
/*****************************************************************************/

int plugin_get_metadata(
    void *ptr, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t *num_creds_md, grpc_status_code *status,
    const char **error_details)
{
    HHVM_TRACE_SCOPE("CallCredentials plugin_get_metadata") // Degug Trace

    plugin_state* const pState{ reinterpret_cast<plugin_state *>(ptr) };

    Object returnObj{ SystemLib::AllocStdClassObject() };
    returnObj.o_set("service_url", String(context.service_url, CopyString));
    returnObj.o_set("method_name", String(context.method_name, CopyString));

    Variant retVal{ vm_call_user_func(pState->callback, make_packed_array(returnObj)) };
    if (!retVal.isArray())
    {
        SystemLib::throwInvalidArgumentExceptionObject("Callback return value expected an array.");
    }

    *num_creds_md = 0;
    *status = GRPC_STATUS_OK;
    *error_details = NULL;

    MetadataArray metadata;
    if (!metadata.init(retVal.toArray()))
    {
        *status = GRPC_STATUS_INVALID_ARGUMENT;
        return true;
    }

    if (metadata.size() > GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX)
    {
        *status = GRPC_STATUS_INTERNAL;
        *error_details = gpr_strdup("PHP plugin credentials returned too many metadata entries");
    }
    else
    {
        // Return data to core.
        *num_creds_md = metadata.size();
        for (size_t i = 0; i < metadata.size(); ++i)
        {
            creds_md[i] = metadata.data()[i];

            // TODO:
            // Right now we Increase the ref of each slice by 1 because it will be decreased by 1
            // when this function goes out of scope and MetadataArray is destructed
            // which then destructs (derefs) the Slice's it's holding.
            // Really what we probably need to add some sort of copy method MetadataArray
            // so that the slices it holds don't become invalid
            gpr_slice_ref(creds_md[i].key);
            gpr_slice_ref(creds_md[i].value);
        }
    }
    return true;  // Synchronous return.
}

void plugin_destroy_state(void *ptr)
{
    HHVM_TRACE_SCOPE("CallCredentials plugin_destroy_state") // Degug Trace

    plugin_state* const pState{ reinterpret_cast<plugin_state *>(ptr) };
    if (pState) gpr_free(pState);
}

} // namespace HPHP
