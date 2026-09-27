include(GoogleTest)

# GoogleTest - Google Testing and Mocking Framework
FetchContent_Declare(
  GoogleTest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        063de7e9578f82b369302001269680b4b1553359 # v1.18.0
)
