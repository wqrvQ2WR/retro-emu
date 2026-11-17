plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.hayoonjae.retroemu"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.hayoonjae.retroemu"
        minSdk = 28
        targetSdk = 36
        versionCode = 1
        versionName = "1.0.0"
        ndk { abiFilters += "arm64-v8a" }
    }

    ndkVersion = "27.2.12479018"
    externalNativeBuild {
        ndkBuild { path = file("src/main/jni/Android.mk") }
    }
    packaging {
        jniLibs { useLegacyPackaging = true }
    }

    buildTypes {
        release {
            // 디버그 빌드는 컴포즈가 느려서 폰에는 릴리스로 깐다
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    buildFeatures {
        compose = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.activity.compose)

    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)

    testImplementation(libs.junit)
    testImplementation("org.json:json:20240303")  // 단위 테스트에서 진짜 JSON 파서(안드로이드 것은 빈 껍데기)
}
