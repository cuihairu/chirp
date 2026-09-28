// chirp Android shell — the repository dir itself is the single :app module
// (sources stay at src/main/kotlin for the SDK-free make gate; the
// Gradle-only app shell lives in src/app/kotlin, wired in below).
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "chirp-android"
