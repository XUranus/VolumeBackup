#include "JVolumeCopyMountUtil.h"
#include "jni.h"
#include "VolumeCopyMountProvider.h"

/* Source for class JVolumeCopyMountUtil */

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
 * Class:     JVolumeCopyMountUtil
 * Method:    buildMountProvider
 * Signature: (LJVolumeCopyMountConfig;)J
 */
JNIEXPORT jlong JNICALL Java_JVolumeCopyMountUtil_buildMountProvider
  (JNIEnv* jEnv, jobject jSelf, jobject jMountConfObj)
{
    // Get the class and field IDs
    jclass mountConfClass = jEnv->GetObjectClass(jMountConfObj);
    jfieldID outputDirPathField = jEnv->GetFieldID(mountConfClass, "outputDirPath", "Ljava/lang/String;");
    jfieldID copyNameField = jEnv->GetFieldID(mountConfClass, "copyName", "Ljava/lang/String;");
    jfieldID copyMetaDirPathField = jEnv->GetFieldID(mountConfClass, "copyMetaDirPath", "Ljava/lang/String;");
    jfieldID copyDataDirPathField = jEnv->GetFieldID(mountConfClass, "copyDataDirPath", "Ljava/lang/String;");
    jfieldID mountTargetPathField = jEnv->GetFieldID(mountConfClass, "mountTargetPath", "Ljava/lang/String;");
    jfieldID mountFsTypeField = jEnv->GetFieldID(mountConfClass, "mountFsType", "Ljava/lang/String;");
    jfieldID mountOptionsField = jEnv->GetFieldID(mountConfClass, "mountOptions", "Ljava/lang/String;");

    // Get the field values
    jstring jOutputDirPath = (jstring)jEnv->GetObjectField(jMountConfObj, outputDirPathField);
    jstring jCopyName = (jstring)jEnv->GetObjectField(jMountConfObj, copyNameField);
    jstring jCopyMetaDirPath = (jstring)jEnv->GetObjectField(jMountConfObj, copyMetaDirPathField);
    jstring jCopyDataDirPath = (jstring)jEnv->GetObjectField(jMountConfObj, copyDataDirPathField);
    jstring jMountTargetPath = (jstring)jEnv->GetObjectField(jMountConfObj, mountTargetPathField);
    jstring jMountFsType = (jstring)jEnv->GetObjectField(jMountConfObj, mountFsTypeField);
    jstring jMountOptions = (jstring)jEnv->GetObjectField(jMountConfObj, mountOptionsField);

    // Convert jstring fields to std::string
    std::string outputDirPath = JString2String(jEnv, jOutputDirPath);
    std::string copyName = JString2String(jEnv, jCopyName);
    std::string copyMetaDirPath = JString2String(jEnv, jCopyMetaDirPath);
    std::string copyDataDirPath = JString2String(jEnv, jCopyDataDirPath);
    std::string mountTargetPath = JString2String(jEnv, jMountTargetPath);
    std::string mountFsType = JString2String(jEnv, jMountFsType);
    std::string mountOptions = JString2String(jEnv, jMountOptions);

    VolumeCopyMountConfig mountConfig {};
    mountConfig.outputDirPath = outputDirPath;
    mountConfig.copyName = copyName;
    mountConfig.copyMetaDirPath = copyMetaDirPath;
    mountConfig.copyDataDirPath = copyDataDirPath;
    mountConfig.mountTargetPath = mountTargetPath;
    mountConfig.mountFsType = mountFsType;
    mountConfig.mountOptions = mountOptions;

    return Ptr2JLong(VolumeCopyMountProvider::Build(mountConfig).release());
}

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    mount0
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_JVolumeCopyMountUtil_mount0
  (JNIEnv* jEnv, jobject jSelf, jlong jProvider)
{
    VolumeCopyMountProvider* mountProvider = reinterpret_cast<VolumeCopyMountProvider*>(Jlong2Ptr(jProvider));
    if (mountProvider == nullptr) {
        return Bool2JBoolean(false);
    }
    return Bool2JBoolean(mountProvider->Mount());
}

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    getMountRecordPath
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_JVolumeCopyMountUtil_getMountRecordPath
  (JNIEnv* jEnv, jobject jSelf, jlong jProvider)
{
    VolumeCopyMountProvider* mountProvider = reinterpret_cast<VolumeCopyMountProvider*>(Jlong2Ptr(jProvider));
    if (mountProvider == nullptr) {
        return nullptr;
    }
    return Str2JString(jEnv, mountProvider->GetMountRecordPath());
}

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    destroyMountProvider
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_JVolumeCopyMountUtil_destroyMountProvider
  (JNIEnv* jEnv, jobject jSelf, jlong jProvider)
{
    VolumeCopyMountProvider* mountProvider = reinterpret_cast<VolumeCopyMountProvider*>(Jlong2Ptr(jProvider));
    if (mountProvider != nullptr) {
        return;
    }
    delete mountProvider;
}

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    buildUmountProvider
 * Signature: (Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_JVolumeCopyMountUtil_buildUmountProvider__Ljava_lang_String_2
  (JNIEnv* jEnv, jobject jSelf, jstring jJsonRecordPath)
{
    std::string jsonRecordPath = JString2String(jEnv, jJsonRecordPath);
    return Ptr2JLong(VolumeCopyUmountProvider::Build(jsonRecordPath).release());
}

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    umount0
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_JVolumeCopyMountUtil_umount0
  (JNIEnv* jEnv, jobject jSelf, jlong jProvider)
{
    VolumeCopyUmountProvider* umountProvider = reinterpret_cast<VolumeCopyUmountProvider*>(Jlong2Ptr(jProvider));
    if (umountProvider == nullptr) {
        return Bool2JBoolean(false);
    }
    return Bool2JBoolean(umountProvider->Umount());
}

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    destroyUmountProvider
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_JVolumeCopyMountUtil_destroyUmountProvider
  (JNIEnv* jEnv, jobject jSelf, jlong jProvider)
{
    VolumeCopyUmountProvider* umountProvider = reinterpret_cast<VolumeCopyUmountProvider*>(Jlong2Ptr(jProvider));
    if (umountProvider != nullptr) {
        return;
    }
    delete umountProvider;
}

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    getError
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_JVolumeCopyMountUtil_getError
  (JNIEnv* jEnv, jobject jSelf, jlong jProvider)
{
    InnerErrorLoggerTrait* errorLogger = reinterpret_cast<InnerErrorLoggerTrait*>(Jlong2Ptr(jProvider));
    if (errorLogger != nullptr) {
        return nullptr;
    }
    return Str2JString(jEnv, errorLogger->GetError());
}


#ifdef __cplusplus
}
#endif