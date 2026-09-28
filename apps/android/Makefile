# SDK-free gate for the Android protocol core (Kotlin JVM): only a JDK 21 +
# kotlinc, no Android SDK/Gradle. Since M3 there is also a Gradle app shell
# (see README); this gate stays the fast, dependency-light leg and compiles
# everything under src/main/kotlin + src/test/kotlin (NOT src/app/kotlin,
# which needs android.jar).
#
#   make test    # fetch pinned jars (sha256-verified), compile, run tests
#   make clean
#
# The protobuf runtime jar is NOT vendored (same convention as the Unity
# csharp runtime): fetched once into .cache/ (gitignored) with a pinned
# checksum. The version must match the protoc that generated proto/java
# (libprotoc 33.4 -> protobuf-java 4.33.4 gencode guard). okhttp/okio back
# the real WebSocket transport; mockwebserver drives it over real sockets in
# tests and drags in junit4 (MockWebServer extends ExternalResource).

KOTLINC ?= kotlinc
JAVA ?= java

PROTOBUF_JAR_URL := https://repo1.maven.org/maven2/com/google/protobuf/protobuf-java/4.33.4/protobuf-java-4.33.4.jar
PROTOBUF_JAR_SHA256 := 3ca892fd6ea8b37d01bb6917dbc0bf2637548b756753f65a28d4f1d4d982347f
JUNIT_JAR_URL := https://repo1.maven.org/maven2/org/junit/platform/junit-platform-console-standalone/1.10.3/junit-platform-console-standalone-1.10.3.jar
JUNIT_JAR_SHA256 := 1455afad75d4eb4f44b493febc4a1bc32bc5f02eb25679361abd55e0f6421050
OKHTTP_JAR_URL := https://repo1.maven.org/maven2/com/squareup/okhttp3/okhttp/4.12.0/okhttp-4.12.0.jar
OKHTTP_JAR_SHA256 := b1050081b14bb7a3a7e55a4d3ef01b5dcfabc453b4573a4fc019767191d5f4e0
OKIO_JAR_URL := https://repo1.maven.org/maven2/com/squareup/okio/okio-jvm/3.6.0/okio-jvm-3.6.0.jar
OKIO_JAR_SHA256 := 67543f0736fc422ae927ed0e504b98bc5e269fda0d3500579337cb713da28412
MOCKWEBSERVER_JAR_URL := https://repo1.maven.org/maven2/com/squareup/okhttp3/mockwebserver/4.12.0/mockwebserver-4.12.0.jar
MOCKWEBSERVER_JAR_SHA256 := 6784673687f4ac8f21679b9d4bc7cdb46e1a1ce1be9d3133b36bede59a741561
JUNIT4_JAR_URL := https://repo1.maven.org/maven2/junit/junit/4.13.2/junit-4.13.2.jar
JUNIT4_JAR_SHA256 := 8e495b634469d64fb8acfa3495a065cbacc8a0fff55ce1e31007be4c16dc57d3

CACHE_DIR := .cache
PROTOBUF_JAR := $(CACHE_DIR)/protobuf-java-4.33.4.jar
JUNIT_JAR := $(CACHE_DIR)/junit-platform-console-standalone-1.10.3.jar
OKHTTP_JAR := $(CACHE_DIR)/okhttp-4.12.0.jar
OKIO_JAR := $(CACHE_DIR)/okio-jvm-3.6.0.jar
MOCKWEBSERVER_JAR := $(CACHE_DIR)/mockwebserver-4.12.0.jar
JUNIT4_JAR := $(CACHE_DIR)/junit-4.13.2.jar

# Main classpath: protobuf + the WebSocket stack. Test classpath adds the
# mock server + junit4 (supertype of MockWebServer).
MAIN_CP := $(PROTOBUF_JAR):$(OKHTTP_JAR):$(OKIO_JAR)
TEST_CP := $(MAIN_CP):$(MOCKWEBSERVER_JAR):$(JUNIT4_JAR)

BUILD_DIR := build
# Kotlin stdlib + kotlin-test ship with the compiler distribution.
KOTLIN_LIB := $(dir $(shell command -v $(KOTLINC)))../lib
KOTLIN_STDLIB := $(KOTLIN_LIB)/kotlin-stdlib.jar
# kotlin-test façade + its JUnit5 binding (tests run on the JUnit console).
KOTLIN_TEST := $(KOTLIN_LIB)/kotlin-test.jar:$(KOTLIN_LIB)/kotlin-test-junit5.jar

.PHONY: test clean jars

test: jars build-classes run-tests

jars: $(PROTOBUF_JAR) $(JUNIT_JAR) $(OKHTTP_JAR) $(OKIO_JAR) $(MOCKWEBSERVER_JAR) $(JUNIT4_JAR)

$(CACHE_DIR):
	mkdir -p $(CACHE_DIR)

$(PROTOBUF_JAR): | $(CACHE_DIR)
	curl -sfL -o $@ $(PROTOBUF_JAR_URL)
	echo "$(PROTOBUF_JAR_SHA256)  $@" | sha256sum -c -

$(JUNIT_JAR): | $(CACHE_DIR)
	curl -sfL -o $@ $(JUNIT_JAR_URL)
	echo "$(JUNIT_JAR_SHA256)  $@" | sha256sum -c -

$(OKHTTP_JAR): | $(CACHE_DIR)
	curl -sfL -o $@ $(OKHTTP_JAR_URL)
	echo "$(OKHTTP_JAR_SHA256)  $@" | sha256sum -c -

$(OKIO_JAR): | $(CACHE_DIR)
	curl -sfL -o $@ $(OKIO_JAR_URL)
	echo "$(OKIO_JAR_SHA256)  $@" | sha256sum -c -

$(MOCKWEBSERVER_JAR): | $(CACHE_DIR)
	curl -sfL -o $@ $(MOCKWEBSERVER_JAR_URL)
	echo "$(MOCKWEBSERVER_JAR_SHA256)  $@" | sha256sum -c -

$(JUNIT4_JAR): | $(CACHE_DIR)
	curl -sfL -o $@ $(JUNIT4_JAR_URL)
	echo "$(JUNIT4_JAR_SHA256)  $@" | sha256sum -c -

build-classes: $(PROTOBUF_JAR)
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/gencode $(BUILD_DIR)/main $(BUILD_DIR)/test
	# protoc java gencode is plain Java: javac first, Kotlin compiles against it.
	$(JAVA) -version 2>&1 | head -1
	find ../../proto/java -name '*.java' > $(BUILD_DIR)/gencode-sources.txt
	$(JAVA) -version >/dev/null 2>&1
	javac -cp $(PROTOBUF_JAR) -d $(BUILD_DIR)/gencode @$(BUILD_DIR)/gencode-sources.txt
	$(KOTLINC) src/main/kotlin -cp $(MAIN_CP):$(BUILD_DIR)/gencode -d $(BUILD_DIR)/main
	$(KOTLINC) src/test/kotlin -cp $(MAIN_CP):$(BUILD_DIR)/gencode:$(BUILD_DIR)/main:$(TEST_CP):$(JUNIT_JAR):$(KOTLIN_TEST) -d $(BUILD_DIR)/test

run-tests:
	$(JAVA) -cp $(BUILD_DIR)/gencode:$(BUILD_DIR)/main:$(BUILD_DIR)/test:$(MAIN_CP):$(TEST_CP):$(KOTLIN_STDLIB):$(KOTLIN_TEST):$(JUNIT_JAR) \
		org.junit.platform.console.ConsoleLauncher --scan-class-path --disable-banner

clean:
	rm -rf $(BUILD_DIR)
