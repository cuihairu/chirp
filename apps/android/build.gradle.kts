// Root-module Android application (no :app submodule — keeps the existing
// src/main/kotlin protocol core and src/test/kotlin JVM tests in place; the
// make gate keeps compiling those two sets without the Android SDK, this
// build additionally compiles the shell in src/app/kotlin against
// android.jar and runs the same JVM tests through Gradle.
plugins {
    id("com.android.application") version "9.4.1"
    // Push build switch (see below): declared `apply false` so the
    // conditional apply() below can resolve it; untouched builds never run it.
    id("com.google.gms.google-services") version "4.5.0" apply false
    // AGP 9 ships built-in Kotlin support: org.jetbrains.kotlin.android is
    // rejected outright ("no longer required since AGP 9.0"). .kt sources in
    // the android sourceSets just compile; jvmTarget follows compileOptions.
}

// Push build switch (M3.5): `-PchirpPush=true` wires firebase-messaging +
// the google-services plugin + the Firebase token source
// (src/push/kotlin). The default build — no flag — compiles the no-op token
// source (src/nopush/kotlin) and touches no Firebase artifact, keeping the
// local gate deterministic. google-services.json in this dir is a
// committed PLACEHOLDER (fabricated credentials, documented in its _note
// and in README/TODO): the plugin parses it, runtime token fetches fail,
// and registration degrades to dart's empty-token path. Real credentials =
// replace that one file, zero code changes.
val pushEnabled = providers.gradleProperty("chirpPush").orNull?.toBoolean() ?: false

android {
    namespace = "chirp.mobile"
    compileSdk = 36

    defaultConfig {
        applicationId = "chirp.mobile"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"
    }

    sourceSets {
        getByName("main") {
            // proto/java is the committed protobuf gencode (plain Java, the
            // same tree the make gate javac-compiles first); Kotlin compiles
            // against it in the same sourceSet. The push token source is a
            // same-FQCN pair (src/push vs src/nopush) chosen by the switch —
            // the shell only sees chirp.mobile.push.PlatformPushTokenSource.
            java.srcDirs(
                "src/main/kotlin",
                "src/app/kotlin",
                "../../proto/java",
                if (pushEnabled) "src/push/kotlin" else "src/nopush/kotlin",
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
        // The protocol core uses java.time-free but Java-9+ APIs
        // (CompletableFuture.failedFuture): Android ships them only from
        // API 31, and minSdk is 26 — desugaring rewrites the calls for older
        // devices. The JVM make gate runs the real JDK and needs none of
        // this.
        isCoreLibraryDesugaringEnabled = true
    }

    buildTypes {
        // Dev shell: cleartext ws:// is the point (see the manifest).
        getByName("release") {
            isMinifyEnabled = false
        }
    }
}

// Only the push build runs the plugin (generates resources from
// google-services.json for FirebaseApp auto-init). Applied after the
// android plugin, the required order for google-services.
if (pushEnabled) {
    apply(plugin = "com.google.gms.google-services")
}

dependencies {
    coreLibraryDesugaring("com.android.tools:desugar_jdk_libs:2.1.5")
    // Must match the protoc that generated proto/java (libprotoc 33.4
    // gencode guard) — same pin as the make gate's Makefile.
    implementation("com.google.protobuf:protobuf-java:4.33.4")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    // Push build switch: only the ON build fetches Firebase (25.1.3, google()
    // maven); the OFF build has no Firebase class on any classpath.
    if (pushEnabled) {
        implementation("com.google.firebase:firebase-messaging:25.1.3")
    }

    // The same JVM tests the make gate runs (protocol core + word filter),
    // through Gradle: JUnit 5 platform + kotlin-test binding + the real
    // MockWebServer suite. junit4 rides along because MockWebServer
    // extends ExternalResource (class loading resolves the supertype).
    // kotlin-test pins the stdlib it matches on the test classpath. Version
    // tied to AGP 9.4.1's built-in Kotlin compiler (2.2.0, reads metadata
    // up to 2.3.0): 2.4.x stdlib breaks the test compile with a metadata
    // version error.
    testImplementation("org.jetbrains.kotlin:kotlin-test:2.2.20")
    testImplementation("org.jetbrains.kotlin:kotlin-test-junit5:2.2.20")
    testImplementation("org.junit.jupiter:junit-jupiter:5.10.3")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher:1.10.3")
    testImplementation("com.squareup.okhttp3:mockwebserver:4.12.0")
    testImplementation("junit:junit:4.13.2")
}

tasks.withType<Test> {
    useJUnitPlatform()
    // Loaded dev box: run the suite serially like the make gate does.
    systemProperty("junit.jupiter.execution.parallel.enabled", false)
}
