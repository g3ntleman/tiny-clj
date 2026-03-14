# Embedded-vs-flash-fs source contract for Clojure library deployment.
#
# Each embedded entry is encoded as:
#   <repo-relative-source-path>|<generated-include-file-name>

set(TINYCLJ_EMBEDDED_CLOJURE_CORE_ENTRIES
    "libs/clojure/core.clj|clojure.core.clj.inc"
    "libs/clojure/core/async.clj|clojure.core.async.clj.inc"
    "libs/clojure/string.clj|clojure.string.clj.inc"
    "libs/clojure/repl.clj|clojure.repl.clj.inc"
    "libs/clojure/pprint.clj|clojure.pprint.clj.inc"
    "libs/clojure/stacktrace.clj|clojure.stacktrace.clj.inc"
    "libs/tiny-clj/runtime.clj|tiny-clj.runtime.clj.inc"
    "libs/tiny-db/kv.clj|tiny-db.kv.clj.inc"
    "libs/tiny-clj/datetime.clj|tiny-clj.datetime.clj.inc"
    "libs/tiny-clj/fs.clj|tiny-clj.fs.clj.inc"
    "libs/tiny-clj/gpio.clj|tiny-clj.gpio.clj.inc"
    "libs/tiny-clj/board.clj|tiny-clj.board.clj.inc"
    "libs/tiny-clj/button.clj|tiny-clj.button.clj.inc"
    "libs/tiny-clj/event.clj|tiny-clj.event.clj.inc"
    "libs/tiny-clj/sensor.clj|tiny-clj.sensor.clj.inc"
    "libs/tiny-clj/net.clj|tiny-clj.net.clj.inc"
    "libs/tiny-clj/net/mdns.clj|tiny-clj.net.mdns.clj.inc"
    "libs/tiny-db/rrd.clj|tiny-db.rrd.clj.inc"
    "libs/tiny-db/rrd-classic.clj|tiny-db.rrd-classic.clj.inc"
    "libs/tiny-db/rrd-spline.clj|tiny-db.rrd-spline.clj.inc"
)

set(TINYCLJ_EMBEDDED_CLOJURE_TINY_FX_ENTRIES
    "libs/tiny-fx/sound.clj|tiny-fx.sound.clj.inc"
    "libs/tiny-fx/sound-native.clj|tiny-fx.sound-native.clj.inc"
    "libs/tiny-fx/gfx-scene.clj|tiny-gfx.scene.clj.inc"
    "libs/tiny-fx/gfx.clj|tiny-gfx.runtime.clj.inc"
    "libs/tiny-fx/startup.clj|tiny-fx.startup.clj.inc"
    "libs/tiny-fx/game-demo.clj|tiny-fx.game-demo.clj.inc"
)

set(TINYCLJ_EMBEDDED_CLOJURE_DEBUG_ENTRIES
    "libs/tiny-fx/sound-debug.clj|tiny-fx.sound-debug.clj.inc"
    "libs/tiny-fx/gfx-collision.clj|tiny-gfx.collision.clj.inc"
    "libs/tiny-fx/gfx-bench.clj|tiny-fx.gfx-bench.clj.inc"
    "libs/tiny-fx/sound-demos.clj|tiny-fx.sound-demos.clj.inc"
    "libs/tiny-fx/sound-demos-data.clj|tiny-fx.sound-demos-data.clj.inc"
    "libs/tiny-fx/sound-demos-william.clj|tiny-fx.sound-demos-william.clj.inc"
)

set(TINYCLJ_FLASH_FS_CLOJURE_SOURCES
    "libs/tiny-fx/sound-demos.clj"
    "libs/tiny-fx/sound-demos-data.clj"
    "libs/tiny-fx/sound-demos-william.clj"
)

function(tinyclj_get_embedded_clojure_entries out_var)
    set(entries ${TINYCLJ_EMBEDDED_CLOJURE_CORE_ENTRIES})
    if(TINYCLJ_WITH_TINY_FX)
        list(APPEND entries
            ${TINYCLJ_EMBEDDED_CLOJURE_TINY_FX_ENTRIES}
            ${TINYCLJ_EMBEDDED_CLOJURE_DEBUG_ENTRIES}
        )
    endif()
    set(${out_var} "${entries}" PARENT_SCOPE)
endfunction()

function(tinyclj_setup_embedded_clojure_generation repo_root build_root outputs_var include_dir_var flash_manifest_var)
    set(include_dir "${build_root}/generated/embedded_clojure")
    set(script_path "${repo_root}/scripts/generate_embedded_clojure_source.py")

    tinyclj_get_embedded_clojure_entries(entries)
    set(outputs)
    foreach(entry IN LISTS entries)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 rel_input)
        list(GET parts 1 rel_output)
        set(output_path "${include_dir}/${rel_output}")
        list(APPEND outputs "${output_path}")
    endforeach()

    set(flash_manifest_path "${build_root}/generated/embedded_clojure/flash_fs_sources.txt")

    # ESP-IDF runs component CMakeLists.txt twice: first in a scriptable
    # requirements pass where add_custom_command/add_custom_target are
    # forbidden. Only emit build rules in the normal configure pass.
    if(NOT CMAKE_SCRIPT_MODE_FILE)
        if(NOT TINYCLJ_PYTHON3_EXECUTABLE)
            find_program(TINYCLJ_PYTHON3_EXECUTABLE NAMES python3 REQUIRED)
        endif()

        file(MAKE_DIRECTORY "${include_dir}")

        foreach(entry IN LISTS entries)
            string(REPLACE "|" ";" parts "${entry}")
            list(GET parts 0 rel_input)
            list(GET parts 1 rel_output)

            set(input_path "${repo_root}/${rel_input}")
            set(output_path "${include_dir}/${rel_output}")
            get_filename_component(output_dir "${output_path}" DIRECTORY)

            add_custom_command(
                OUTPUT "${output_path}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
                COMMAND "${TINYCLJ_PYTHON3_EXECUTABLE}" "${script_path}"
                    --input "${input_path}"
                    --output "${output_path}"
                    --symbol "${rel_output}"
                DEPENDS
                    "${input_path}"
                    "${script_path}"
                COMMENT "Generating embedded Clojure include ${rel_output}"
                VERBATIM
            )
        endforeach()

        string(REPLACE ";" "\n" flash_manifest_content "${TINYCLJ_FLASH_FS_CLOJURE_SOURCES}")
        file(GENERATE OUTPUT "${flash_manifest_path}" CONTENT "${flash_manifest_content}\n")
    endif()

    set(${outputs_var} "${outputs}" PARENT_SCOPE)
    set(${include_dir_var} "${include_dir}" PARENT_SCOPE)
    set(${flash_manifest_var} "${flash_manifest_path}" PARENT_SCOPE)
endfunction()
