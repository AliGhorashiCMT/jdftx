find_path(FFTW3_INCLUDE_DIR fftw3.h ${FFTW3_PATH} ${FFTW3_PATH}/include NO_DEFAULT_PATH)
find_path(FFTW3_INCLUDE_DIR fftw3.h)
find_library(FFTW3_LIBRARY NAMES fftw3 PATHS ${FFTW3_PATH} ${FFTW3_PATH}/lib ${FFTW3_PATH}/lib64 NO_DEFAULT_PATH)
find_library(FFTW3_LIBRARY NAMES fftw3)
find_library(FFTW3_THREADS_LIBRARY NAMES fftw3_threads PATHS ${FFTW3_PATH} ${FFTW3_PATH}/lib ${FFTW3_PATH}/lib64 NO_DEFAULT_PATH)
find_library(FFTW3_THREADS_LIBRARY NAMES fftw3_threads)

if(FFTW3_INCLUDE_DIR AND FFTW3_LIBRARY AND FFTW3_THREADS_LIBRARY)
	set(FFTW3_FOUND TRUE)
endif()


if(FFTW3_FOUND)
	if(NOT FFTW3_FIND_QUIETLY)
		message(STATUS "Found FFTW3: ${FFTW3_THREADS_LIBRARY} ${FFTW3_LIBRARY}")
	endif()
else()
	if(FFTW3_FIND_REQUIRED)
		if(NOT FFTW3_INCLUDE_DIR)
			message(FATAL_ERROR "Could not find FFTW3 headers (Add -D FFTW3_PATH=<path> to the cmake commandline for a non-standard installation)")
		endif()
		if(NOT FFTW3_LIBRARY)
			message(FATAL_ERROR "Could not find FFTW3 shared libraries (Add -D FFTW3_PATH=<path> to the cmake commandline for a non-standard installation)")
		endif()
		if(NOT FFTW3_THREADS_LIBRARY)
			message(FATAL_ERROR "Could not find threaded FFTW3 shared libraries (Add -D FFTW3_PATH=<path> to the cmake commandline for a non-standard installation)")
		endif()
	endif()
endif()
