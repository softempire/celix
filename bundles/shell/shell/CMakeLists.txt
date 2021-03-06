# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
# 
#   http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
celix_subproject(SHELL "Option to enable building the Shell bundles" ON DEPS LAUNCHER LOG_SERVICE)
if (SHELL)
	find_package(CURL REQUIRED)

	add_library(shell_api INTERFACE)
	target_include_directories(shell_api INTERFACE
			$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/api>
			$<INSTALL_INTERFACE:include/celix/shell>
	)
	install(TARGETS shell_api EXPORT celix COMPONENT shell)
	install(DIRECTORY api/ DESTINATION include/celix/shell COMPONENT shell)

	if (CELIX_INSTALL_DEPRECATED_API)
		target_include_directories(shell_api INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/deprecated_api>)
		install(DIRECTORY deprecated_api/ DESTINATION include/celix/shell COMPONENT shell)
	endif ()

    add_celix_bundle(shell
        SYMBOLIC_NAME "apache_celix_shell"
        VERSION "2.1.0"
        NAME "Apache Celix Shell"
		GROUP "Celix/Shell"
        SOURCES
          src/activator
          src/shell
          src/lb_command
          src/start_command
          src/stop_command
          src/install_command
          src/update_command
          src/uninstall_command
          src/help_command
		  src/dm_shell_list_command
		  src/query_command.c
		  src/quit_command.c
	)
	target_include_directories(shell PRIVATE src)
	target_link_libraries(shell PRIVATE Celix::shell_api CURL::libcurl Celix::log_service_api Celix::log_helper)
	target_compile_options(shell PRIVATE -Wno-deprecated-declarations)
	if (CELIX_INSTALL_DEPRECATED_API)
		target_compile_definitions(shell PRIVATE CELIX_INSTALL_DEPRECATED_API)
	endif ()

	install_celix_bundle(shell EXPORT celix COMPONENT shell)

	#Setup target aliases to match external usage
	add_library(Celix::shell_api ALIAS shell_api)
	add_library(Celix::shell ALIAS shell)


	if (ENABLE_TESTING)
		add_subdirectory(test)
	endif()
endif (SHELL)
