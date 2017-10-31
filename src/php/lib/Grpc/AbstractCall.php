<?php
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

namespace Grpc;

/**
 * Class AbstractCall.
 * @package Grpc
 */
abstract class AbstractCall
{
    /**
     * @var Call
     */
    protected $call;
    protected $deserialize;
    protected $metadata;
    protected $trailing_metadata;

    /**
     * Create a new Call wrapper object.
     *
     * @param Channel  $channel     The channel to communicate on
     * @param string   $method      The method to call on the
     *                              remote server
     * @param callback $deserialize A callback function to deserialize
     *                              the response
     * @param array    $options     Call options (optional)
     */
    public function __construct(Channel $channel,
                                $method,
                                $deserialize,
                                array $options = [])
    {
        if (array_key_exists('timeout', $options) &&
            is_numeric($timeout = $options['timeout'])
        ) {
            QMetric::startBenchmark('app_time_grpc_timeval');
            $now = Timeval::now();
            $delta = new Timeval($timeout);
            $deadline = $now->add($delta);
            QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_timeval');
        } else {
            QMetric::startBenchmark('app_time_grpc_timeval');
            $deadline = Timeval::infFuture();
            QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_timeval');
        }

        QMetric::startBenchmark('app_time_grpc_call_construct');
        $this->call = new Call($channel, $method, $deadline);
        QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_call_construct');

        $this->deserialize = $deserialize;
        $this->metadata = null;
        $this->trailing_metadata = null;
        if (array_key_exists('call_credentials_callback', $options) &&
            is_callable($call_credentials_callback =
                $options['call_credentials_callback'])
        ) {
            QMetric::startBenchmark('app_time_grpc_call_creds_createfromplugin');
            $call_credentials = CallCredentials::createFromPlugin(
                $call_credentials_callback
            );
            QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_call_creds_createfromplugin');
            QMetric::startBenchmark('app_time_grpc_call_setcredentials');
            $this->call->setCredentials($call_credentials);
            QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_call_setcredentials');
        }
    }

    /**
     * @return mixed The metadata sent by the server
     */
    public function getMetadata()
    {
        return $this->metadata;
    }

    /**
     * @return mixed The trailing metadata sent by the server
     */
    public function getTrailingMetadata()
    {
        return $this->trailing_metadata;
    }

    /**
     * @return string The URI of the endpoint
     */
    public function getPeer()
    {
        QMetric::startBenchmark('app_time_grpc_call_getpeer');
        $peer = $this->call->getPeer();
        QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_call_getpeer');
        return $peer;
    }

    /**
     * Cancels the call.
     */
    public function cancel()
    {
        QMetric::startBenchmark('app_time_grpc_call_cancel');
        $this->call->cancel();
        QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_call_cancel');
    }

    /**
     * Serialize a message to the protobuf binary format.
     *
     * @param mixed $data The Protobuf message
     *
     * @return string The protobuf binary format
     */
    protected function _serializeMessage($data)
    {
        // Proto3 implementation
        if (method_exists($data, 'encode')) {
            return $data->encode();
        } elseif (method_exists($data, 'serializeToString')) {
            return $data->serializeToString();
        }

        // Protobuf-PHP implementation
        return $data->serialize();
    }

    /**
     * Deserialize a response value to an object.
     *
     * @param string $value The binary value to deserialize
     *
     * @return mixed The deserialized value
     */
    protected function _deserializeResponse($value)
    {
        if ($value === null) {
            return;
        }

        // Proto3 implementation
        if (is_array($this->deserialize)) {
            list($className, $deserializeFunc) = $this->deserialize;
            $obj = new $className();
            if (method_exists($obj, $deserializeFunc)) {
                $obj->$deserializeFunc($value);
            } else {
                $obj->mergeFromString($value);
            }

            return $obj;
        }

        // Protobuf-PHP implementation
        return call_user_func($this->deserialize, $value);
    }

    /**
     * Set the CallCredentials for the underlying Call.
     *
     * @param CallCredentials $call_credentials The CallCredentials object
     */
    public function setCallCredentials($call_credentials)
    {
        QMetric::startBenchmark('app_time_grpc_call_setcredentials');
        $this->call->setCredentials($call_credentials);
        QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_call_setcredentials');
    }
}
