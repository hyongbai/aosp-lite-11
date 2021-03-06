/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "CachedAppOptimizer"
//#define LOG_NDEBUG 0

#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <android-base/stringprintf.h>
#include <android-base/file.h>

#include <nativehelper/JNIHelp.h>
#include <android_runtime/AndroidRuntime.h>
#include <jni.h>

using android::base::StringPrintf;
using android::base::WriteStringToFile;

namespace android {

// This performs per-process reclaim on all processes belonging to non-app UIDs.
// For the most part, these are non-zygote processes like Treble HALs, but it
// also includes zygote-derived processes that run in system UIDs, like bluetooth
// or potentially some mainline modules. The only process that should definitely
// not be compacted is system_server, since compacting system_server around the
// time of BOOT_COMPLETE could result in perceptible issues.
static void com_android_server_am_CachedAppOptimizer_compactSystem(JNIEnv *, jobject) {
    std::unique_ptr<DIR, decltype(&closedir)> proc(opendir("/proc"), closedir);
    struct dirent* current;
    while ((current = readdir(proc.get()))) {
        if (current->d_type != DT_DIR) {
            continue;
        }

        // don't compact system_server, rely on persistent compaction during screen off
        // in order to avoid mmap_sem-related stalls
        if (atoi(current->d_name) == getpid()) {
            continue;
        }

        std::string status_name = StringPrintf("/proc/%s/status", current->d_name);
        struct stat status_info;

        if (stat(status_name.c_str(), &status_info) != 0) {
            // must be some other directory that isn't a pid
            continue;
        }

        // android.os.Process.FIRST_APPLICATION_UID
        if (status_info.st_uid >= 10000) {
            continue;
        }

        std::string reclaim_path = StringPrintf("/proc/%s/reclaim", current->d_name);
        WriteStringToFile(std::string("all"), reclaim_path);
    }
}

static const JNINativeMethod sMethods[] = {
    /* name, signature, funcPtr */
    {"compactSystem", "()V", (void*)com_android_server_am_CachedAppOptimizer_compactSystem},
};

int register_android_server_am_CachedAppOptimizer(JNIEnv* env)
{
    return jniRegisterNativeMethods(env, "com/android/server/am/CachedAppOptimizer",
                                    sMethods, NELEM(sMethods));
}

}
