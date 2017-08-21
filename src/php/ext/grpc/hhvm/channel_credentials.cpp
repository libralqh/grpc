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

#include <shared_mutex>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "channel_credentials.h"
#include "call_credentials.h"
#include "common.h"

#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/string-util.h"

#include "grpc/support/alloc.h"

namespace HPHP {

/*****************************************************************************/
/*                     Default Permanent Root Certificates                   */
/*****************************************************************************/

static std::shared_timed_mutex s_DefaultPermRootCertsMutex;

DefaultPermRootCerts::DefaultPermRootCerts(void) : m_PermRootCerts{}
{
}

grpc_ssl_roots_override_result DefaultPermRootCerts::get_ssl_roots_override(char** pPermRootsCerts)
{
    // The output of this function (the char *) is gpr_free'd by the calling function
    // so we must std::memcpy it before passing it to pPermRootsCerts
    // if we also want to manage it ourselves / share it with others

    *pPermRootsCerts = getDefaultPermRootCerts().getCerts().c_str();
    if (!(*pPermRootsCerts))
    {
        return GRPC_SSL_ROOTS_OVERRIDE_FAIL;
    }
    else
    {
        return GRPC_SSL_ROOTS_OVERRIDE_OK;
    }
}

Slice DefaultPermRootCerts::getCerts(void)
{
    std::shared_lock<std::shared_timed_mutex> lock{ s_DefaultPermRootCertsMutex };
    return m_PermRootCerts;
}

void DefaultPermRootCerts::setCerts(const String& permRootsCerts)
{
    // optimistic locking
    if (getCerts().string() == permRootsCerts) return;

    std::unique_lock<std::shared_timed_mutex> lock{ s_DefaultPermRootCertsMutex };

    if (m_PermRootCerts.string() == permRootsCerts) return;
    std::cout << "setCerts" << std::endl;

    // copy new certs
    m_PermRootCerts = Slice { permRootsCerts };
}

DefaultPermRootCerts& DefaultPermRootCerts::getDefaultPermRootCerts(void)
{
    static DefaultPermRootCerts s_DefaultPermRootCerts{};
    return s_DefaultPermRootCerts;
}

/*****************************************************************************/
/*                        Channel Credentials Data                           */
/*****************************************************************************/

Class* ChannelCredentialsData::s_pClass{ nullptr };
const StaticString ChannelCredentialsData::s_ClassName{ "Grpc\\ChannelCredentials" };

Class* const ChannelCredentialsData::getClass(void)
{
    if (!s_pClass)
    {
        s_pClass = Unit::lookupClass(s_ClassName.get());
        assert(s_pClass);
    }
    return s_pClass;
}

ChannelCredentialsData::ChannelCredentialsData(void) :
    m_pChannelCredentials{ nullptr }, m_HashKey{ String{} }
{
}

ChannelCredentialsData::~ChannelCredentialsData(void)
{
    destroy();
}

void ChannelCredentialsData::init(grpc_channel_credentials* const pChannelCredentials,
                                  const String& hashKey)
{
    // destroy any existing channel credentials
    destroy();

    m_pChannelCredentials = pChannelCredentials;
    m_HashKey = hashKey;
}

void ChannelCredentialsData::destroy(void)
{
    if (m_pChannelCredentials)
    {
        grpc_channel_credentials_release(m_pChannelCredentials);
        m_pChannelCredentials = nullptr;
    }
}

/*****************************************************************************/
/*                     HHVM Channel Credentials Methods                      */
/*****************************************************************************/

void HHVM_STATIC_METHOD(ChannelCredentials, setDefaultRootsPem,
                        const String& perm_root_certs)
{
    HHVM_TRACE_SCOPE("ChannelCredentials setDefaultRootsPem") // Degug Trace

    DefaultPermRootCerts::getDefaultPermRootCerts().setCerts(perm_root_certs);
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createDefault)
{
    HHVM_TRACE_SCOPE("ChannelCredentials createDefault") // Degug Trace

    Object newChannelCredentialsObj{ ChannelCredentialsData::getClass() };
    ChannelCredentialsData* const pChannelCredentialsData{ Native::data<ChannelCredentialsData>(newChannelCredentialsObj) };
    grpc_channel_credentials* const pChannelCredentials{ grpc_google_default_credentials_create() };

    if (!pChannelCredentials)
    {
        SystemLib::throwBadMethodCallExceptionObject("Failed to create default channel credentials");
    }

    pChannelCredentialsData->init(pChannelCredentials);

    return newChannelCredentialsObj;
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createSsl,
                          const Variant& perm_root_certs /*=null*/,
                          const Variant& perm_key_cert_pair__private_key /*= null*/,
                          const Variant& perm_key_cert_pair__cert_chain /*=null*/
                          )
{
    HHVM_TRACE_SCOPE("ChannelCredentials createSsl") // Degug Trace

    const char* const pPermRootCerts{ (!perm_root_certs.isNull() && perm_root_certs.isString()) ?
                                      perm_root_certs.toString().c_str() : nullptr };

    Object newChannelCredentialsObj{ ChannelCredentialsData::getClass() };
    ChannelCredentialsData* const pChannelCredentialsData{ Native::data<ChannelCredentialsData>(newChannelCredentialsObj) };

    String unhashedKey{};
    grpc_ssl_pem_key_cert_pair perm_key_cert_pair;
    perm_key_cert_pair.private_key = perm_key_cert_pair.cert_chain = nullptr;

    if (!perm_key_cert_pair__private_key.isNull() && perm_key_cert_pair__private_key.isString())
    {
        perm_key_cert_pair.private_key = perm_key_cert_pair__private_key.toString().c_str();
        unhashedKey += perm_key_cert_pair__private_key.toString();
    }

    if (!perm_key_cert_pair__cert_chain.isNull() && perm_key_cert_pair__cert_chain.isString())
    {
        perm_key_cert_pair.cert_chain = perm_key_cert_pair__cert_chain.toString().c_str();
        unhashedKey += perm_key_cert_pair__cert_chain.toString();
    }

    String hashKey{ !unhashedKey.empty() ? StringUtil::SHA1(unhashedKey, false) : "" };

    grpc_channel_credentials* const pChannelCredentials{
        grpc_ssl_credentials_create(pPermRootCerts,
                                    !perm_key_cert_pair.private_key ? nullptr : &perm_key_cert_pair,
                                    nullptr) };

    if (!pChannelCredentials)
    {
        SystemLib::throwBadMethodCallExceptionObject("Failed to create SSL channel credentials");
    }

    pChannelCredentialsData->init(pChannelCredentials,  hashKey);

    return newChannelCredentialsObj;
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createComposite,
                          const Object& cred1_obj,
                          const Object& cred2_obj)
{
    HHVM_TRACE_SCOPE("ChannelCredentials createComposite") // Degug Trace

    ChannelCredentialsData* pChannelCredentialsData{ Native::data<ChannelCredentialsData>(cred1_obj) };
    CallCredentialsData* pCallCredentialsData{ Native::data<CallCredentialsData>(cred2_obj) };

    grpc_channel_credentials* const pChannelCredentials{
        grpc_composite_channel_credentials_create(pChannelCredentialsData->credentials(),
                                                  pCallCredentialsData->credentials(), nullptr) };

    if (!pChannelCredentials)
    {
        SystemLib::throwBadMethodCallExceptionObject("Failed to create composite channel credentials");
    }

    Object newChannelCredentialsObj{ChannelCredentialsData::getClass() };
    ChannelCredentialsData* const pNewChannelCredentialsData{ Native::data<ChannelCredentialsData>(newChannelCredentialsObj) };
    pNewChannelCredentialsData->init(pChannelCredentials, pChannelCredentialsData->hashKey());

    return newChannelCredentialsObj;
}

Variant HHVM_STATIC_METHOD(ChannelCredentials, createInsecure)
{
    HHVM_TRACE_SCOPE("ChannelCredentials createInsecure") // Degug Trace

    return Variant{};
}

void grpc_hhvm_init_channel_credentials(void)
{
    grpc_set_ssl_roots_override_callback(DefaultPermRootCerts::get_ssl_roots_override);
}

} // namespace HPHP
