#include "catch.hpp"
#include "c-compat.hpp"

extern "C" {
#include <memory.h>
#include <strscpy.h>
}

SCENARIO("strscpy()") {
	char dst[256];
	memset(dst, 0xff, sizeof(dst));

	GIVEN("Empty input string") {
		THEN("-E2BIG is returned for zero size") {
			CHECK(strscpy(dst, "", 0) == -E2BIG);
		}

		THEN("Zero is returned for size 1") {
			CHECK(strscpy(dst, "", 1) == 0);
			CHECK(std::string{""} == dst);
		}

		THEN("Zero is returned for size 2") {
			CHECK(strscpy(dst, "", 2) == 0);
			CHECK(std::string{""} == dst);
		}


		THEN("Zero is returned for size 5") {
			CHECK(strscpy(dst, "", 5) == 0);
			CHECK(std::string{""} == dst);
		}
	}

	GIVEN("Non-empty input string") {
		WHEN("There is enough space") {
			THEN("Copying string of length 1 yields the string succeeds") {
				CHECK(strscpy(dst, "a", 2) == 1);
				CHECK(std::string{"a"} == dst);
			}

			THEN("Copying string of length 3 yields the string succeeds") {
				CHECK(strscpy(dst, "abc", 5) == 3);
				CHECK(std::string{"abc"} == dst);
			}
		}

		WHEN("There is not enough space") {
			THEN("Copying string of length 1 fails") {
				CHECK(strscpy(dst, "a", 1) < 0);
			}

			THEN("Copying string of length 3 fails") {
				CHECK(strscpy(dst, "abc", 3) < 0);
				CHECK(strscpy(dst, "abc", 2) < 0);
				CHECK(strscpy(dst, "abc", 1) < 0);
			}
		}
	}
}
