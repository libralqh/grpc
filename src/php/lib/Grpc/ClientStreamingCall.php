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
 * Represents an active call that sends a stream of messages and then gets
 * a single response.
 */
class ClientStreamingCall extends AbstractCall
{
    /**
     * Start the call.
     *
     * @param array $metadata Metadata to send with the call, if applicable
     *                        (optional)
     */
    public function start(array $metadata = [])
    {
        QMetric::startBenchmark('app_time_grpc_startbatch');
        $this->call->startBatch([
            OP_SEND_INITIAL_METADATA => $metadata,
        ]);
        QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_startbatch');
    }

    /**
     * Write a single message to the server. This cannot be called after
     * wait is called.
     *
     * @param ByteBuffer $data    The data to write
     * @param array      $options An array of options, possible keys:
     *                            'flags' => a number (optional)
     */
    public function write($data, array $options = [])
    {
        $message_array = ['message' => $this->_serializeMessage($data)];
        if (array_key_exists('flags', $options)) {
            $message_array['flags'] = $options['flags'];
        }
        QMetric::startBenchmark('app_time_grpc_startbatch');
        $this->call->startBatch([
            OP_SEND_MESSAGE => $message_array,
        ]);
        QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_startbatch');
    }

    /**
     * Wait for the server to respond with data and a status.
     *
     * @return array [response data, status]
     */
    public function wait()
    {
        QMetric::startBenchmark('app_time_grpc_startbatch');
        $event = $this->call->startBatch([
            OP_SEND_CLOSE_FROM_CLIENT => true,
            OP_RECV_INITIAL_METADATA => true,
            OP_RECV_MESSAGE => true,
            OP_RECV_STATUS_ON_CLIENT => true,
        ]);
        QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_startbatch');
        $this->metadata = $event->metadata;

        $status = $event->status;
        $this->trailing_metadata = $status->metadata;

        return [$this->_deserializeResponse($event->message), $status];
    }
}
