#include "JLinuxVolumeCopyMountProvider.h"
/* Source for class JLinuxVolumeCopyMountProvider */

#ifndef _Included_JLinuxVolumeCopyMountProvider
#define _Included_JLinuxVolumeCopyMountProvider
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    createMountProvider
 * Signature: (Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_JLinuxVolumeCopyMountProvider_createMountProvider
  (JNIEnv *, jobject, jstring)
{

}

/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    mountVolumeCopy
 * Signature: (JLLinuxVolumeCopyMountConfig;)Z
 */
JNIEXPORT jboolean JNICALL Java_JLinuxVolumeCopyMountProvider_mountVolumeCopy
  (JNIEnv *, jobject, jlong, jobject)
{

}

/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    umountVolumeCopy
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_JLinuxVolumeCopyMountProvider_umountVolumeCopy
  (JNIEnv *, jobject, jlong)
{

}

/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    clearResidue
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_JLinuxVolumeCopyMountProvider_clearResidue
  (JNIEnv *, jobject, jlong);

/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    destroyMountProvider
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_JLinuxVolumeCopyMountProvider_destroyMountProvider
  (JNIEnv *, jobject, jlong)
{

}

/*
 * Class:     JLinuxVolumeCopyMountProvider
 * Method:    getMountProviderError
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_JLinuxVolumeCopyMountProvider_getMountProviderError
  (JNIEnv *, jobject, jlong)
{
    
}

#ifdef __cplusplus
}
#endif
#endif
