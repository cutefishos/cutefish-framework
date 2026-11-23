cutefish_add_client(fprint TARGET FprintClient
    SOURCES device_session.cpp fingerprint_enrollment.cpp fingerprint_finger.cpp fingerprint_reader.cpp
    HEADERS fingerprint_enrollment.h fingerprint_finger.h fingerprint_reader.h
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
