# Gate for the Android native protocol core (Kotlin JVM). No Gradle yet:
# the Gradle app shell (AGP + Android SDK + UI) is a later batch; until then
# this Makefile is the whole gate and needs only a JDK 21 + kotlinc.
#
#   make test    # fetch pinned jars (sha256-verified), compile, run tests
#   make clean
#
# The protobuf runtime jar is NOT vendored (same convention as the Unity
# csharp runtime): fetched once into .cache/ (gitignored) with a pinned
# checksum. The version must match the protoc that generated proto/java
# (libprotoc 33.4 -> protobuf-java 4.33.4 gencode guard).

KOTLINC ?= kotlinc
JAVA ?= java

PROTOBUF_JAR_URL := https://repo1.maven.org/maven2/com/google/protobuf/protobuf-java/4.33.4/protobuf-java-4.33.4.jar
PROTOBUF_JAR_SHA256 := 3ca892fd6ea8b37d01bb6917dbc0bf2637548b756753f65a28d4f1d4d982347f
JUNIT_JAR_URL := https://repo1.maven.org/maven2/org/junit/platform/junit-platform-console-standalone/1.10.3/junit-platform-console-standalone-1.10.3.jar
JUNIT_JAR_SHA256 := 1455afad75d4eb4f44b493febc4a1bc32bc5f02eb25679361abd55e0f6421050

CACHE_DIR := .cache
PROTOBUF_JAR := $(CACHE_DIR)/protobuf-java-4.33.4.jar
JUNIT_JAR := $(CACHE_DIR)/junit-platform-console-standalone-1.10.3.jar

BUILD_DIR := build
# Kotlin stdlib + kotlin-test ship with the compiler distribution.
KOTLIN_LIB := $(dir $(shell command -v $(KOTLINC)))../lib
KOTLIN_STDLIB := $(KOTLIN_LIB)/kotlin-stdlib.jar
# kotlin-test façade + its JUnit5 binding (tests run on the JUnit console).
KOTLIN_TEST := $(KOTLIN_LIB)/kotlin-test.jar:$(KOTLIN_LIB)/kotlin-test-junit5.jar

.PHONY: test clean jars

test: jars build-classes run-tests

jars: $(PROTOBUF_JAR) $(JUNIT_JAR)

$(CACHE_DIR):
	mkdir -p $(CACHE_DIR)

$(PROTOBUF_JAR): | $(CACHE_DIR)
	curl -sfL -o $@ $(PROTOBUF_JAR_URL)
	echo "$(PROTOBUF_JAR_SHA256)  $@" | sha256sum -c -

$(JUNIT_JAR): | $(CACHE_DIR)
	curl -sfL -o $@ $(JUNIT_JAR_URL)
	echo "$(JUNIT_JAR_SHA256)  $@" | sha256sum -c -

build-classes: $(PROTOBUF_JAR)
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/gencode $(BUILD_DIR)/main $(BUILD_DIR)/test
	# protoc java gencode is plain Java: javac first, Kotlin compiles against it.
	$(JAVA) -version 2>&1 | head -1
	find ../../proto/java -name '*.java' > $(BUILD_DIR)/gencode-sources.txt
	$(JAVA) -version >/dev/null 2>&1
	javac -cp $(PROTOBUF_JAR) -d $(BUILD_DIR)/gencode @$(BUILD_DIR)/gencode-sources.txt
	$(KOTLINC) src/main/kotlin -cp $(PROTOBUF_JAR):$(BUILD_DIR)/gencode -d $(BUILD_DIR)/main
	$(KOTLINC) src/test/kotlin -cp $(PROTOBUF_JAR):$(BUILD_DIR)/gencode:$(BUILD_DIR)/main:$(JUNIT_JAR):$(KOTLIN_TEST) -d $(BUILD_DIR)/test

run-tests:
	$(JAVA) -cp $(BUILD_DIR)/gencode:$(BUILD_DIR)/main:$(BUILD_DIR)/test:$(PROTOBUF_JAR):$(KOTLIN_STDLIB):$(KOTLIN_TEST):$(JUNIT_JAR) \
		org.junit.platform.console.ConsoleLauncher --scan-class-path --disable-banner

clean:
	rm -rf $(BUILD_DIR)
