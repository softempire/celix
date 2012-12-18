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
 * topology_manager.c
 *
 *  \date       Sep 29, 2011
 *  \author    	<a href="mailto:celix-dev@incubator.apache.org">Apache Celix Project Team</a>
 *  \copyright	Apache License, Version 2.0
 */
#include <stdio.h>
#include <stdlib.h>

#include <apr_uuid.h>
#include <apr_strings.h>

#include "topology_manager.h"
#include "bundle_context.h"
#include "constants.h"
#include "module.h"
#include "bundle.h"
#include "remote_service_admin.h"
#include "remote_constants.h"
#include "filter.h"
#include "listener_hook_service.h"
#include "utils.h"
#include "service_reference.h"
#include "service_registration.h"

struct topology_manager {
	apr_pool_t *pool;
	bundle_context_t context;

	array_list_t rsaList;
	hash_map_t exportedServices;
	hash_map_t importedServices;

	hash_map_t importInterests;
};

struct import_interest {
	char *filter;
	int refs;
};

celix_status_t topologyManager_notifyListeners(topology_manager_t manager, remote_service_admin_service_t rsa,  array_list_t registrations);
celix_status_t topologyManager_notifyListenersOfRemoval(topology_manager_t manager, remote_service_admin_service_t rsa,  export_registration_t export);

celix_status_t topologyManager_getUUID(topology_manager_t manager, char **uuidStr);

celix_status_t topologyManager_create(bundle_context_t context, apr_pool_t *pool, topology_manager_t *manager) {
	celix_status_t status = CELIX_SUCCESS;

	*manager = apr_palloc(pool, sizeof(**manager));
	if (!*manager) {
		status = CELIX_ENOMEM;
	} else {
		(*manager)->pool = pool;
		(*manager)->context = context;
		(*manager)->rsaList = NULL;
		arrayList_create(pool, &(*manager)->rsaList);
		(*manager)->exportedServices = hashMap_create(serviceReference_hashCode, NULL, serviceReference_equals2, NULL);
		(*manager)->importedServices = hashMap_create(NULL, NULL, NULL, NULL);
		(*manager)->importInterests = hashMap_create(string_hash, NULL, string_equals, NULL);
	}

	return status;
}

celix_status_t topologyManager_rsaAdding(void * handle, service_reference_t reference, void **service) {
	celix_status_t status = CELIX_SUCCESS;
	topology_manager_t manager = handle;

	status = bundleContext_getService(manager->context, reference, service);

	return status;
}

celix_status_t topologyManager_rsaAdded(void * handle, service_reference_t reference, void * service) {
	celix_status_t status = CELIX_SUCCESS;
	topology_manager_t manager = handle;

	printf("TOPOLOGY_MANAGER: Added RSA\n");
	arrayList_add(manager->rsaList, service);

	return status;
}

celix_status_t topologyManager_rsaModified(void * handle, service_reference_t reference, void * service) {
	celix_status_t status = CELIX_SUCCESS;
	topology_manager_t manager = handle;
	return status;
}

celix_status_t topologyManager_rsaRemoved(void * handle, service_reference_t reference, void * service) {
	celix_status_t status = CELIX_SUCCESS;
	topology_manager_t manager = handle;

	printf("TOPOLOGY_MANAGER: Removed RSA\n");
	arrayList_removeElement(manager->rsaList, service);

	return status;
}

celix_status_t topologyManager_serviceChanged(void *listener, service_event_t event) {
	celix_status_t status = CELIX_SUCCESS;
	service_listener_t listen = listener;
	topology_manager_t manager = listen->handle;
	service_registration_t registration = NULL;
	serviceReference_getServiceRegistration(event->reference, &registration);
	properties_t props = NULL;
	serviceRegistration_getProperties(registration, &props);
	char *name = properties_get(props, (char *) OBJECTCLASS);
	char *export = properties_get(props, (char *) SERVICE_EXPORTED_INTERFACES);

	if (event->type == SERVICE_EVENT_REGISTERED) {
		if (export != NULL) {
			printf("TOPOLOGY_MANAGER: Service registered: %s\n", name);
			status = topologyManager_exportService(manager, event->reference);
		}
	} else if (event->type == SERVICE_EVENT_UNREGISTERING) {
		//if (export != NULL) {
			printf("TOPOLOGY_MANAGER: Service unregistering: %s\n", name);
			status = topologyManager_removeService(manager, event->reference);
		//}
	}

	return status;
}

celix_status_t topologyManager_endpointAdded(void *handle, endpoint_description_t endpoint, char *machtedFilter) {
	celix_status_t status = CELIX_SUCCESS;
	topology_manager_t manager = handle;
	printf("TOPOLOGY_MANAGER: Endpoint added\n");

	status = topologyManager_importService(manager, endpoint);

	return status;
}

celix_status_t topologyManager_endpointRemoved(void *handle, endpoint_description_t endpoint, char *machtedFilter) {
	celix_status_t status = CELIX_SUCCESS;
	topology_manager_t manager = handle;
	printf("TOPOLOGY_MANAGER: Endpoint removed\n");

	if (hashMap_containsKey(manager->importedServices, endpoint)) {
		hash_map_t imports = hashMap_get(manager->importedServices, endpoint);
		hash_map_iterator_t iter = hashMapIterator_create(imports);
		while (hashMapIterator_hasNext(iter)) {
			hash_map_entry_t entry = hashMapIterator_nextEntry(iter);
			remote_service_admin_service_t rsa = hashMapEntry_getKey(entry);
			import_registration_t import = hashMapEntry_getValue(entry);
			rsa->importRegistration_close(import);
		}
	}

	return status;
}

celix_status_t topologyManager_exportService(topology_manager_t manager, service_reference_t reference) {
	celix_status_t status = CELIX_SUCCESS;
	hash_map_t exports = hashMap_create(NULL, NULL, NULL, NULL);

	hashMap_put(manager->exportedServices, reference, exports);

	if (arrayList_size(manager->rsaList) == 0) {
		char *symbolicName = NULL;
		module_t module = NULL;
		bundle_t bundle = NULL;
		serviceReference_getBundle(reference, &bundle);
		status = bundle_getCurrentModule(bundle, &module);
		if (status == CELIX_SUCCESS) {
			status = module_getSymbolicName(module, &symbolicName);
			if (status == CELIX_SUCCESS) {
				printf("TOPOLOGY_MANAGER: No RemoteServiceAdmin available, unable to export service from bundle %s.\n", symbolicName);
			}
		}
	} else {
		int size = arrayList_size(manager->rsaList);
		int iter = 0;
		for (iter = 0; iter < size; iter++) {
			remote_service_admin_service_t rsa = arrayList_get(manager->rsaList, iter);

			array_list_t endpoints = NULL;
			status = rsa->exportService(rsa->admin, reference, NULL, &endpoints);
			if (status == CELIX_SUCCESS) {
				hashMap_put(exports, rsa, endpoints);
				status = topologyManager_notifyListeners(manager, rsa, endpoints);
			}
		}
	}

	return status;
}

celix_status_t topologyManager_notifyListeners(topology_manager_t manager, remote_service_admin_service_t rsa,  array_list_t registrations) {
	celix_status_t status = CELIX_SUCCESS;
	array_list_t endpointListeners = NULL;

	status = bundleContext_getServiceReferences(manager->context, endpoint_listener_service, NULL, &endpointListeners);
	if (status == CELIX_SUCCESS) {
		if (endpointListeners != NULL) {
			int eplIt;
			for (eplIt = 0; eplIt < arrayList_size(endpointListeners); eplIt++) {
				service_reference_t eplRef = arrayList_get(endpointListeners, eplIt);
				service_registration_t registration = NULL;
				serviceReference_getServiceRegistration(eplRef, &registration);
				properties_t props = NULL;
				serviceRegistration_getProperties(registration, &props);
				char *scope = properties_get(props, (char *) ENDPOINT_LISTENER_SCOPE);
				filter_t filter = filter_create(scope, manager->pool);
				endpoint_listener_t epl = NULL;
				status = bundleContext_getService(manager->context, eplRef, (void **) &epl);
				if (status == CELIX_SUCCESS) {
					int regIt;
					for (regIt = 0; regIt < arrayList_size(registrations); regIt++) {
						export_registration_t export = arrayList_get(registrations, regIt);
						export_reference_t reference = NULL;
						endpoint_description_t endpoint = NULL;
						status = rsa->exportRegistration_getExportReference(export, &reference);
						if (status == CELIX_SUCCESS) {
							status = rsa->exportReference_getExportedEndpoint(reference, &endpoint);
							if (status == CELIX_SUCCESS) {
								bool matchResult = false;
								filter_match(filter, endpoint->properties, &matchResult);
								if (matchResult) {
									status = epl->endpointAdded(epl->handle, endpoint, scope);
								}
							}
						}
					}
				}
			}
		}
	}

	return status;
}

celix_status_t topologyManager_importService(topology_manager_t manager, endpoint_description_t endpoint) {
	celix_status_t status = CELIX_SUCCESS;
	hash_map_t imports = hashMap_create(NULL, NULL, NULL, NULL);

	hashMap_put(manager->importedServices, endpoint, imports);

	if (arrayList_size(manager->rsaList) == 0) {
		printf("TOPOLOGY_MANAGER: No RemoteServiceAdmin available, unable to import service %s.\n", endpoint->service);
	} else {
		int size = arrayList_size(manager->rsaList);
		int iter = 0;
		for (iter = 0; iter < size; iter++) {
			remote_service_admin_service_t rsa = arrayList_get(manager->rsaList, iter);

			import_registration_t import = NULL;
			status = rsa->importService(rsa->admin, endpoint, &import);
			if (status == CELIX_SUCCESS) {
				hashMap_put(imports, rsa, import);
			}
		}
	}

	return status;
}

celix_status_t topologyManager_removeService(topology_manager_t manager, service_reference_t reference) {
	celix_status_t status = CELIX_SUCCESS;

	service_registration_t registration = NULL;
	serviceReference_getServiceRegistration(reference, &registration);
	properties_t props = NULL;
	serviceRegistration_getProperties(registration, &props);
	char *name = properties_get(props, (char *) OBJECTCLASS);

	printf("TOPOLOGY_MANAGER: Remove Service: %s.\n", name);

	if (hashMap_containsKey(manager->exportedServices, reference)) {
		hash_map_t exports = hashMap_get(manager->exportedServices, reference);
		hash_map_iterator_t iter = hashMapIterator_create(exports);
		while (hashMapIterator_hasNext(iter)) {
			hash_map_entry_t entry = hashMapIterator_nextEntry(iter);
			remote_service_admin_service_t rsa = hashMapEntry_getKey(entry);
			array_list_t exports = hashMapEntry_getValue(entry);
			int exportsIter = 0;
			for (exportsIter = 0; exportsIter < arrayList_size(exports); exportsIter++) {
				export_registration_t export = arrayList_get(exports, exportsIter);
				rsa->exportRegistration_close(export);
				topologyManager_notifyListenersOfRemoval(manager, rsa, export);
			}

		}
	}

	return status;
}

celix_status_t topologyManager_notifyListenersOfRemoval(topology_manager_t manager, remote_service_admin_service_t rsa,  export_registration_t export) {
	celix_status_t status = CELIX_SUCCESS;
	array_list_t endpointListeners = NULL;

	status = bundleContext_getServiceReferences(manager->context, endpoint_listener_service, NULL, &endpointListeners);
	if (status == CELIX_SUCCESS) {
		if (endpointListeners != NULL) {
			int eplIt;
			for (eplIt = 0; eplIt < arrayList_size(endpointListeners); eplIt++) {
				service_reference_t eplRef = arrayList_get(endpointListeners, eplIt);
				endpoint_listener_t epl = NULL;
				status = bundleContext_getService(manager->context, eplRef, (void **) &epl);
				if (status == CELIX_SUCCESS) {
					export_reference_t reference = NULL;
					endpoint_description_t endpoint = NULL;
					status = rsa->exportRegistration_getExportReference(export, &reference);
					if (status == CELIX_SUCCESS) {
						status = rsa->exportReference_getExportedEndpoint(reference, &endpoint);
						if (status == CELIX_SUCCESS) {
							status = epl->endpointRemoved(epl->handle, endpoint, NULL);
						}
					}
				}
			}
		}
	}

	return status;
}

celix_status_t topologyManager_extendFilter(topology_manager_t manager, char *filter, char **updatedFilter) {
	celix_status_t status = CELIX_SUCCESS;
	apr_pool_t *pool = NULL;
	apr_pool_create(&pool, manager->pool);

	char *uuid = NULL;
	topologyManager_getUUID(manager, &uuid);
	*updatedFilter = apr_pstrcat(pool, "(&", filter, "(!(", ENDPOINT_FRAMEWORK_UUID, "=", uuid, ")))", NULL);

	return status;
}

celix_status_t topologyManager_listenerAdded(void *handle, array_list_t listeners) {
	celix_status_t status = CELIX_SUCCESS;

	topology_manager_t manager = handle;
	int i;
	for (i = 0; i < arrayList_size(listeners); i++) {
		listener_hook_info_t info = arrayList_get(listeners, i);
		printf("TOPOLOGY_MANAGER: listener with filter \"%s\" added\n", info->filter);

		bundle_t bundle, self;
		bundleContext_getBundle(info->context, &bundle);
		bundleContext_getBundle(manager->context, &self);
		if (bundle == self) {
			printf("TOPOLOGY_MANAGER: Ignore myself\n");
			continue;
		}

		char *filter;
		topologyManager_extendFilter(manager, info->filter, &filter);

		struct import_interest *interest = hashMap_get(manager->importInterests, filter);
		if (interest != NULL) {
			interest->refs++;
		} else {
			apr_pool_t *pool = NULL;
			apr_pool_create(&pool, manager->pool);
			interest = apr_palloc(pool, sizeof(*interest));
			interest->filter = filter;
			interest->refs = 1;
			hashMap_put(manager->importInterests, filter, interest);
//			endpointListener.extendScope(exFilter);
		}
	}

	return status;
}

celix_status_t topologyManager_listenerRemoved(void *handle, array_list_t listeners) {
	celix_status_t status = CELIX_SUCCESS;

	topology_manager_t manager = handle;
	int i;
	for (i = 0; i < arrayList_size(listeners); i++) {
		listener_hook_info_t info = arrayList_get(listeners, i);
		printf("TOPOLOGY_MANAGER: listener with filter \"%s\" removed\n", info->filter);

		char *filter;
		topologyManager_extendFilter(manager, info->filter, &filter);

		struct import_interest *interest = hashMap_get(manager->importInterests, filter);
		if (interest != NULL) {
			if (interest->refs-- <= 0) {
				// last reference, remove from scope
//				endpointListener.reduceScope(exFilter);
				hashMap_remove(manager->importInterests, filter);

				// clean up import registrations
//				List<ImportRegistration> irs = importedServices.remove(exFilter);
//				if (irs != null) {
//					for (ImportRegistration ir : irs) {
//						if (ir != null) {
//							ir.close();
//						}
//					}
//				}
			}
		}
	}

	return status;
}

celix_status_t topologyManager_getUUID(topology_manager_t manager, char **uuidStr) {
	celix_status_t status = CELIX_SUCCESS;
	apr_pool_t *pool = NULL;
	apr_pool_create(&pool, manager->pool);

	status = bundleContext_getProperty(manager->context, ENDPOINT_FRAMEWORK_UUID, uuidStr);
	if (status == CELIX_SUCCESS) {
		if (*uuidStr == NULL) {
			apr_uuid_t uuid;
			apr_uuid_get(&uuid);
			*uuidStr = apr_palloc(pool, APR_UUID_FORMATTED_LENGTH + 1);
			apr_uuid_format(*uuidStr, &uuid);
			setenv(ENDPOINT_FRAMEWORK_UUID, *uuidStr, 1);
		}
	}

	return status;
}