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
 * Represents an active call that sends a single message and then gets a
 * stream of responses.
 */
class ServerStreamingCall extends AbstractCall
{
    /**
     * Start the call.
     *
     * @param mixed $data     The data to send
     * @param array $metadata Metadata to send with the call, if applicable
     *                        (optional)
     * @param array $options  An array of options, possible keys:
     *                        'flags' => a number (optional)
     */
    public function start($data, array $metadata = [], array $options = [])
    {
        $message_array = ['message' => $this->_serializeMessage($data)];
        if (array_key_exists('flags', $options)) {
            $message_array['flags'] = $options['flags'];
        }
        QMetric::startBenchmark('app_time_grpc_startbatch');
        $this->call->startBatch([
            OP_SEND_INITIAL_METADATA => $metadata,
            OP_SEND_MESSAGE => $message_array,
            OP_SEND_CLOSE_FROM_CLIENT => true,
        ]);
        QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_startbatch');
    }

    /**
     * @return mixed An iterator of response values
     */
    public function responses()
    {
        $batch = [OP_RECV_MESSAGE => true];
        if ($this->metadata === null) {
            $batch[OP_RECV_INITIAL_METADATA] = true;
        }
        QMetric::startBenchmark('app_time_grpc_startbatch');
        $read_event = $this->call->startBatch($batch);
        QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_startbatch');
        if ($this->metadata === null) {
            $this->metadata = $read_event->metadata;
        }
        $response = $read_event->message;
        while ($response !== null) {
            yield $this->_deserializeResponse($response);
            QMetric::startBenchmark('app_time_grpc_startbatch');
            $response = $this->call->startBatch([
                OP_RECV_MESSAGE => true,
            ])->message;
            QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_startbatch');
        }
    }

    /**
     * Wait for the server to send the status, and return it.
     *
     * @return \stdClass The status object, with integer $code, string
     *                   $details, and array $metadata members
     */
    public function getStatus()
    {
        QMetric::startBenchmark('app_time_grpc_startbatch');
        $status_event = $this->call->startBatch([
            OP_RECV_STATUS_ON_CLIENT => true,
        ]);
        QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_startbatch');

        $this->trailing_metadata = $status_event->status->metadata;

        return $status_event->status;
    }

    /**
     * @return mixed The metadata sent by the server
     */
    public function getMetadata()
    {
        if ($this->metadata === null) {
            QMetric::startBenchmark('app_time_grpc_startbatch');
            $event = $this->call->startBatch([OP_RECV_INITIAL_METADATA => true]);
            QMetric::timing('spanner.app_time.grpc', 'app_time_grpc_startbatch');
            $this->metadata = $event->metadata;
        }
        return $this->metadata;
    }
}
