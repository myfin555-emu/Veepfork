# Reuse the iOS snapshot format, conflict handling and native iCloud query.
enable_language(Swift)
add_library(veeb-cloud SHARED
    CloudSaveBridge.swift
    ../ios/Vita3K/App/Localization.swift
    ../ios/Vita3K/App/CloudSaveSync.swift
    ../ios/Vita3K/App/SaveSyncEngine.swift
)
set_target_properties(veeb-cloud PROPERTIES
    # The parent directory's -Wformat/-Werror options are Clang-only.
    COMPILE_OPTIONS ""
    Swift_LANGUAGE_VERSION 6
    Swift_MODULE_NAME VeebCloud
    OUTPUT_NAME VeebCloud
    MACOSX_RPATH ON
    INSTALL_NAME_DIR "@rpath"
    BUILD_WITH_INSTALL_NAME_DIR ON
)
target_compile_options(veeb-cloud PRIVATE
    -target "${ARCHITECTURE}-apple-macos${CMAKE_OSX_DEPLOYMENT_TARGET}")
