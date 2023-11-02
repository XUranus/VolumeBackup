/**
 * @copyright Copyright 2023 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */
/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class JVolumeCopyMountUtil */

#ifndef _Included_JVolumeCopyMountUtil
#define _Included_JVolumeCopyMountUtil
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     JVolumeCopyMountUtil
 * Method:    buildMountProvider
 * Signature: (LJVolumeCopyMountConfig;)J
 */
JNIEXPORT jlong JNICALL Java_JVolumeCopyMountUtil_buildMountProvider
  (JNIEnv *, jobject, jobject);

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    mount0
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_JVolumeCopyMountUtil_mount0
  (JNIEnv *, jobject, jlong);

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    getMountRecordPath
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_JVolumeCopyMountUtil_getMountRecordPath
  (JNIEnv *, jobject, jlong);

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    destroyMountProvider
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_JVolumeCopyMountUtil_destroyMountProvider
  (JNIEnv *, jobject, jlong);

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    buildUmountProvider
 * Signature: (Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_JVolumeCopyMountUtil_buildUmountProvider__Ljava_lang_String_2
  (JNIEnv *, jobject, jstring);

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    umount0
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_JVolumeCopyMountUtil_umount0
  (JNIEnv *, jobject, jlong);

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    buildUmountProvider
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_JVolumeCopyMountUtil_buildUmountProvider__J
  (JNIEnv *, jobject, jlong);

/*
 * Class:     JVolumeCopyMountUtil
 * Method:    getError
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_JVolumeCopyMountUtil_getError
  (JNIEnv *, jobject, jlong);

#ifdef __cplusplus
}
#endif
#endif
