if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_executable(CHIRSerialzierTest CHIRSerialzierTest.cpp CHIRDeSerialzierTest.cpp)
    target_link_libraries(
        CHIRSerialzierTest
        cangjie-lsp
        ${LINK_LIBS}
        boundscheck-static
        GTest::gtest
        GTest::gtest_main
        TestCompilerInstanceObject)
    add_dependencies(CHIRSerialzierTest CangjieFlatbuffersHeaders)
    target_include_directories(CHIRSerialzierTest PRIVATE ${FLATBUFFERS_INCLUDE_DIR})
    add_test(NAME CHIRSerialzierTest COMMAND CHIRSerialzierTest)
endif()
