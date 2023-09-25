#include "JLinuxVolumeCopyMountProvider.h"
#include "jni.h"

#include "LinuxVolumeCopyMountProvider.h"

/* Source for class JLinuxVolumeCopyMountProvider */

#include <string>
#include <iostream>

using namespace volumeprotect::mount;

#ifdef __cplusplus
extern "C" {
#endif

static std::string JString2String(JNIEnv* jEnv, const jstring& jstr)
{
    const char* cstr = jEnv->GetStringUTFChars(jstr, nullptr);
    std::string str(cstr == nullptr ? "" : cstr);
    return str;
}

static jstring Str2JString(JNIEnv* jEnv, const std::string& str)
{
    return jEnv->NewStringUTF(str.c_str());
}

static void* Jlong2Ptr(jlong value)
{
    return reinterpret_cast<void*>(value);
}

static jlong Ptr2JLong(void* ptr)
{
    return reinterpret_cast<jlong>(ptr);
}

static jboolean Bool2JBoolean(bool value)
{
    return static_cast<jboolean>(value);
}

static bool JBoolean2Bool(jboolean value)
{
    return static_cast<bool>(value);
}

/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    createMountProvider
 * Signature: (Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_JLinuxVolumeCopyMountProvider_createMountProvider
  (JNIEnv* jEnv, jobject jSelf, jstring jCacheDirPath)
{
    std::string cacheDirPath = JString2String(jEnv, jCacheDirPath);
    void* providerPtr = LinuxMountProvider::BuildLinuxMountProvider(cacheDirPath).release();
    std::cout << "createMountProvider " << cacheDirPath << std::endl;
    return Ptr2JLong(providerPtr);
}

/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    mountVolumeCopy
 * Signature: (JLLinuxVolumeCopyMountConfig;)Z
 */
JNIEXPORT jboolean JNICALL Java_JLinuxVolumeCopyMountProvider_mountVolumeCopy
  (JNIEnv* jEnv, jobject jSelf, jlong jProvider, jobject jMountConfObj)
{
    LinuxMountProvider* mountProvider = reinterpret_cast<LinuxMountProvider*>(Jlong2Ptr(jProvider));
    // Get the class and field IDs
    jclass mountConfClass = jEnv->GetObjectClass(jMountConfObj);
    jfieldID copyMetaDirPathField = jEnv->GetFieldID(mountConfClass, "copyMetaDirPath", "Ljava/lang/String;");
    jfieldID copyDataDirPathField = jEnv->GetFieldID(mountConfClass, "copyDataDirPath", "Ljava/lang/String;");
    jfieldID mountTargetPathField = jEnv->GetFieldID(mountConfClass, "mountTargetPath", "Ljava/lang/String;");
    jfieldID mountFsTypeField = jEnv->GetFieldID(mountConfClass, "mountFsType", "Ljava/lang/String;");
    jfieldID mountOptionsField = jEnv->GetFieldID(mountConfClass, "mountOptions", "Ljava/lang/String;");

    // Get the field values
    jstring jCopyMetaDirPath = (jstring)jEnv->GetObjectField(jMountConfObj, copyMetaDirPathField);
    jstring jCopyDataDirPath = (jstring)jEnv->GetObjectField(jMountConfObj, copyDataDirPathField);
    jstring jMountTargetPath = (jstring)jEnv->GetObjectField(jMountConfObj, mountTargetPathField);
    jstring jMountFsType = (jstring)jEnv->GetObjectField(jMountConfObj, mountFsTypeField);
    jstring jMountOptions = (jstring)jEnv->GetObjectField(jMountConfObj, mountOptionsField);

    // Convert jstring fields to std::string
    std::string copyMetaDirPath = JString2String(jEnv, jCopyMetaDirPath);
    std::string copyDataDirPath = JString2String(jEnv, jCopyDataDirPath);
    std::string mountTargetPath = JString2String(jEnv, jMountTargetPath);
    std::string mountFsType = JString2String(jEnv, jMountFsType);
    std::string mountOptions = JString2String(jEnv, jMountOptions);

    LinuxCopyMountConfig mountConfig {};
    mountConfig.copyMetaDirPath = copyMetaDirPath;
    mountConfig.copyDataDirPath = copyDataDirPath;
    mountConfig.mountTargetPath = mountTargetPath;
    mountConfig.mountFsType = mountFsType;
    mountConfig.mountOptions = mountOptions;

    std::cout << "mountVolumeCopy "
        << copyMetaDirPath << " "
        << copyDataDirPath << " "
        << mountTargetPath << " "
        << mountFsType << " "
        << mountOptions << std::endl;

    return Bool2JBoolean(mountProvider->MountCopy(mountConfig));
}

/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    umountVolumeCopy
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_JLinuxVolumeCopyMountProvider_umountVolumeCopy
  (JNIEnv* jEnv, jobject jSelf, jlong jProvider)
{
    LinuxMountProvider* mountProvider = reinterpret_cast<LinuxMountProvider*>(Jlong2Ptr(jProvider));
    std::cout << "umountVolumeCopy " << mountProvider << std::endl;
    return Bool2JBoolean(mountProvider->UmountCopy());
}

/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    clearResidue
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_JLinuxVolumeCopyMountProvider_clearResidue
  (JNIEnv* jEnv, jobject jSelf, jlong jProvider)
{
    LinuxMountProvider* mountProvider = reinterpret_cast<LinuxMountProvider*>(Jlong2Ptr(jProvider));
    std::cout << "clearResidue " << mountProvider << std::endl;
    return Bool2JBoolean(mountProvider->ClearResidue());
}

/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    destroyMountProvider
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_JLinuxVolumeCopyMountProvider_destroyMountProvider
  (JNIEnv* jEnv, jobject jSelf, jlong jProvider)
{
    LinuxMountProvider* mountProvider = reinterpret_cast<LinuxMountProvider*>(Jlong2Ptr(jProvider));
    std::cout << "destroyMountProvider " << mountProvider << std::endl;
    delete mountProvider;
    return;
}

/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    getMountProviderError
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_JLinuxVolumeCopyMountProvider_getMountProviderError
  (JNIEnv* jEnv, jobject jSelf, jlong jProvider)
{
    LinuxMountProvider* mountProvider = reinterpret_cast<LinuxMountProvider*>(Jlong2Ptr(jProvider));
    std::cout << "getMountProviderError " << mountProvider << std::endl;
    return Str2JString(jEnv, mountProvider->GetErrors());
}

#ifdef __cplusplus
}
#endif