/**
 *Licensed to the Apache Software Foundation (ASF) under one
 *or more contributor license agreements.  See the NOTICE file
 *distributed with this work for additional information
 *regarding copyright ownership.  The ASF licenses this file
 *to you under the Apache License, Version 2.0 (the
 *"License"); you may not use this file except in compliance
 *with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing,
 *software distributed under the License is distributed on an
 *"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 *specific language governing permissions and limitations
 *under the License.
 */
/*
 * log_entry.h
 *
 *  \date       Jun 26, 2011
 *  \author    	<a href="mailto:celix-dev@incubator.apache.org">Apache Celix Project Team</a>
 *  \copyright	Apache License, Version 2.0
 */

#ifndef LOG_ENTRY_H_
#define LOG_ENTRY_H_

#include "log_service.h"

struct log_entry {
    bundle_t bundle;
    int errorCode;
    log_level_t level;
    char *message;
    service_reference_t reference;
    time_t time;
};

typedef struct log_entry * log_entry_t;

celix_status_t logEntry_create(bundle_t bundle, service_reference_t reference,
        log_level_t level, char *message, int errorCode,
        apr_pool_t *pool, log_entry_t *entry);
celix_status_t logEntry_getBundle(log_entry_t entry, bundle_t *bundle);
celix_status_t logEntry_getErrorCode(log_entry_t entry, int *errorCode);
celix_status_t logEntry_getLevel(log_entry_t entry, log_level_t *level);
celix_status_t logEntry_getMessage(log_entry_t entry, char **message);
celix_status_t logEntry_getServiceReference(log_entry_t entry, service_reference_t *reference);
celix_status_t logEntry_getTime(log_entry_t entry, time_t *time);

#endif /* LOG_ENTRY_H_ */