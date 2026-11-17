// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.
// ASTKIND(ast kind, ast value for human reading, ast node, ast size for notify checking related code)
#ifdef DEFINE_MEMBER

// jobject NewObject(JNIEnv *env, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(NewObject, VAR_DECL, 3)
// jobject NewObjectA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(NewObjectA, VAR_DECL, 4)

// jbyte CallByteMethod(JNIEnv *env, jobject obj, jmethodID methodID);
DEFINE_MEMBER(CallByteMethod, VAR_DECL, 3)
// jbyte CallByteMethodA (JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallByteMethodA, VAR_DECL, 4)

// jbyte CallNonvirtualByteMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallNonvirtualByteMethod, VAR_DECL, 4)
// jbyte CallNonvirtualByteMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallNonvirtualByteMethodA, VAR_DECL, 5)

// jbyte CallStaticByteMethod(JNIEnv *env, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallStaticByteMethod, VAR_DECL, 3)
// jbyte CallStaticByteMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallStaticByteMethodA, VAR_DECL, 4)

// jchar CallCharMethod(JNIEnv *env, jobject obj, jmethodID methodID);
DEFINE_MEMBER(CallCharMethod, VAR_DECL, 3)
// jchar CallCharMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallCharMethodA, VAR_DECL, 4)

// jchar CallNonvirtualCharMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallNonvirtualCharMethod, VAR_DECL, 4)
// jchar CallNonvirtualCharMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallNonvirtualCharMethodA, VAR_DECL, 5)

// jchar CallStaticCharMethod(JNIEnv *env, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallStaticCharMethod, VAR_DECL, 3)
// jchar CallStaticCharMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallStaticCharMethodA, VAR_DECL, 4)


// jshort CallShortMethod(JNIEnv *env, jobject obj, jmethodID methodID);
DEFINE_MEMBER(CallShortMethod, VAR_DECL, 3)
// jshort CallShortMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallShortMethodA, VAR_DECL, 4)

// jshort CallNonvirtualShortMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallNonvirtualShortMethod, VAR_DECL, 4)
// jshort CallNonvirtualShortMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallNonvirtualShortMethodA, VAR_DECL, 5)

// jshort CallStaticShortMethod(JNIEnv *env, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallStaticShortMethod, VAR_DECL, 3)
// jshort CallStaticShortMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallStaticShortMethodA, VAR_DECL, 4)


// jint CallIntMethod(JNIEnv *env, jobject obj, jmethodID methodID);
DEFINE_MEMBER(CallIntMethod, VAR_DECL, 3)
// jint CallIntMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallIntMethodA, VAR_DECL, 4)

// jint CallNonvirtualIntMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallNonvirtualIntMethod, VAR_DECL, 4)
// jint CallNonvirtualIntMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallNonvirtualIntMethodA, VAR_DECL, 5)

// jint CallStaticIntMethod(JNIEnv *env, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallStaticIntMethod, VAR_DECL, 3)
// jint CallStaticIntMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallStaticIntMethodA, VAR_DECL, 4)


// jlong CallLongMethod(JNIEnv *env, jobject obj, jmethodID methodID);
DEFINE_MEMBER(CallLongMethod, VAR_DECL, 3)
// jlong CallLongMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallLongMethodA, VAR_DECL, 4)

// jlong CallNonvirtualLongMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallNonvirtualLongMethod, VAR_DECL, 4)
// jlong CallNonvirtualLongMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallNonvirtualLongMethodA, VAR_DECL, 5)

// jlong CallStaticLongMethod(JNIEnv *env, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallStaticLongMethod, VAR_DECL, 3)
// jlong CallStaticLongMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallStaticLongMethodA, VAR_DECL, 4)


// jfloat CallFloatMethod(JNIEnv *env, jobject obj, jmethodID methodID);
DEFINE_MEMBER(CallFloatMethod, VAR_DECL, 3)
// jfloat CallFloatMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallFloatMethodA, VAR_DECL, 4)

// jfloat CallNonvirtualFloatMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallNonvirtualFloatMethod, VAR_DECL, 4)
// jfloat CallNonvirtualFloatMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallNonvirtualFloatMethodA, VAR_DECL, 5)

// jfloat CallStaticFloatMethod(JNIEnv *env, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallStaticFloatMethod, VAR_DECL, 3)
// jfloat CallStaticFloatMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallStaticFloatMethodA, VAR_DECL, 4)


// jdouble CallDoubleMethod(JNIEnv *env, jobject obj, jmethodID methodID);
DEFINE_MEMBER(CallDoubleMethod, VAR_DECL, 3)
// jdouble CallDoubleMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallDoubleMethodA, VAR_DECL, 4)

// jdouble CallNonvirtualDoubleMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallNonvirtualDoubleMethod, VAR_DECL, 4)
// jdouble CallNonvirtualDoubleMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallNonvirtualDoubleMethodA, VAR_DECL, 5)

// jdouble CallStaticDoubleMethod(JNIEnv *env, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallStaticDoubleMethod, VAR_DECL, 3)
// jdouble CallStaticDoubleMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallStaticDoubleMethodA, VAR_DECL, 4)


// jboolean CallBooleanMethod(JNIEnv *env, jobject obj, jmethodID methodID);
DEFINE_MEMBER(CallBooleanMethod, VAR_DECL, 3)
// jboolean CallBooleanMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallBooleanMethodA, VAR_DECL, 4)

// jboolean CallNonvirtualBooleanMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallNonvirtualBooleanMethod, VAR_DECL, 4)
// jboolean CallNonvirtualBooleanMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args)
DEFINE_MEMBER(CallNonvirtualBooleanMethodA, VAR_DECL, 5)

// jboolean CallStaticBooleanMethod(JNIEnv *env, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallStaticBooleanMethod, VAR_DECL, 3)
// jboolean CallStaticBooleanMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallStaticBooleanMethodA, VAR_DECL, 4)


// void CallVoidMethod(JNIEnv *env, jobject obj, jmethodID methodID);
DEFINE_MEMBER(CallVoidMethod, VAR_DECL, 3)
// void CallVoidMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallVoidMethodA, VAR_DECL, 4)

// void CallNonvirtualVoidMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallNonvirtualVoidMethod, VAR_DECL, 4)
// void CallNonvirtualVoidMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallNonvirtualVoidMethodA, VAR_DECL, 5)

// void CallStaticVoidMethod(JNIEnv *env, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallStaticVoidMethod, VAR_DECL, 3)
// void CallStaticVoidMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallStaticVoidMethodA, VAR_DECL, 4)


// jobject CallObjectMethod(JNIEnv *env, jobject obj, jmethodID methodID);
DEFINE_MEMBER(CallObjectMethod, VAR_DECL, 3)
// jobject CallObjectMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallObjectMethodA, VAR_DECL, 4)

// jobject CallNonvirtualObjectMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallNonvirtualObjectMethod, VAR_DECL, 4)
// jobject CallNonvirtualObjectMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallNonvirtualObjectMethodA, VAR_DECL, 5)

// jobject CallStaticObjectMethod(JNIEnv *env, jclass clazz, jmethodID methodID);
DEFINE_MEMBER(CallStaticObjectMethod, VAR_DECL, 3)
// jobject CallStaticObjectMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);
DEFINE_MEMBER(CallStaticObjectMethodA, VAR_DECL, 4)


// jobject GetObjectField(JNIEnv *env, jobject obj, jfieldID fieldID);
DEFINE_MEMBER(GetObjectField, VAR_DECL, 3)
// void SetObjectField(JNIEnv *env, jobject obj, jfieldID fieldID, jobject val);
DEFINE_MEMBER(SetObjectField, VAR_DECL, 4)
// jobject GetStaticObjectField(JNIEnv *env, jclass clazz, jfieldID fieldID);
DEFINE_MEMBER(GetStaticObjectField, VAR_DECL, 3)
// void SetStaticObjectField(JNIEnv *env, jclass clazz, jfieldID fieldID, jobject value);
DEFINE_MEMBER(SetStaticObjectField, VAR_DECL, 4)

// jboolean GetBooleanField(JNIEnv *env, jobject obj, jfieldID fieldID);
DEFINE_MEMBER(GetBooleanField, VAR_DECL, 3)
// void SetBooleanField(JNIEnv *env, jobject obj, jfieldID fieldID, jboolean val);
DEFINE_MEMBER(SetBooleanField, VAR_DECL, 4)
// jboolean GetStaticBooleanField(JNIEnv *env, jclass clazz, jfieldID fieldID);
DEFINE_MEMBER(GetStaticBooleanField, VAR_DECL, 3)
// void SetStaticBooleanField(JNIEnv *env, jclass clazz, jfieldID fieldID, jboolean value);
DEFINE_MEMBER(SetStaticBooleanField, VAR_DECL, 4)

// jbyte GetByteField(JNIEnv *env, jobject obj, jfieldID fieldID);
DEFINE_MEMBER(GetByteField, VAR_DECL, 3)
// void SetByteField(JNIEnv *env, jobject obj, jfieldID fieldID, jbyte val);
DEFINE_MEMBER(SetByteField, VAR_DECL, 4)
// jbyte GetStaticByteField(JNIEnv *env, jclass clazz, jfieldID fieldID);
DEFINE_MEMBER(GetStaticByteField, VAR_DECL, 3)
// void SetStaticByteField(JNIEnv *env, jclass clazz, jfieldID fieldID, jbyte value);
DEFINE_MEMBER(SetStaticByteField, VAR_DECL, 4)

// jchar GetCharField(JNIEnv *env, jobject obj, jfieldID fieldID);
DEFINE_MEMBER(GetCharField, VAR_DECL, 3)
// void SetCharField(JNIEnv *env, jobject obj, jfieldID fieldID, jchar val);
DEFINE_MEMBER(SetCharField, VAR_DECL, 4)
// jchar GetStaticCharField(JNIEnv *env, jclass clazz, jfieldID fieldID);
DEFINE_MEMBER(GetStaticCharField, VAR_DECL, 3)
// void SetStaticCharField(JNIEnv *env, jclass clazz, jfieldID fieldID, jchar value);
DEFINE_MEMBER(SetStaticCharField, VAR_DECL, 4)

// jshort GetShortField(JNIEnv *env, jobject obj, jfieldID fieldID);
DEFINE_MEMBER(GetShortField, VAR_DECL, 3)
// void SetShortField(JNIEnv *env, jobject obj, jfieldID fieldID, jshort val);
DEFINE_MEMBER(SetShortField, VAR_DECL, 4)
// jshort GetStaticShortField(JNIEnv *env, jclass clazz, jfieldID fieldID);
DEFINE_MEMBER(GetStaticShortField, VAR_DECL, 3)
// void SetStaticShortField(JNIEnv *env, jclass clazz, jfieldID fieldID, jshort value);
DEFINE_MEMBER(SetStaticShortField, VAR_DECL, 4)

// jint GetIntField(JNIEnv *env, jobject obj, jfieldID fieldID);
DEFINE_MEMBER(GetIntField, VAR_DECL, 3)
// void SetIntField(JNIEnv *env, jobject obj, jfieldID fieldID, jint val);
DEFINE_MEMBER(SetIntField, VAR_DECL, 4)
// jint GetStaticIntField(JNIEnv *env, jclass clazz, jfieldID fieldID);
DEFINE_MEMBER(GetStaticIntField, VAR_DECL, 3)
// void SetStaticIntField(JNIEnv *env, jclass clazz, jfieldID fieldID, jint value);
DEFINE_MEMBER(SetStaticIntField, VAR_DECL, 4)

// jlong GetLongField(JNIEnv *env, jobject obj, jfieldID fieldID);
DEFINE_MEMBER(GetLongField, VAR_DECL, 3)
// void SetLongField(JNIEnv *env, jobject obj, jfieldID fieldID, jlong val);
DEFINE_MEMBER(SetLongField, VAR_DECL, 4)
// jlong GetStaticLongField(JNIEnv *env, jclass clazz, jfieldID fieldID);
DEFINE_MEMBER(GetStaticLongField, VAR_DECL, 3)
// void SetStaticLongField(JNIEnv *env, jclass clazz, jfieldID fieldID, jlong value);
DEFINE_MEMBER(SetStaticLongField, VAR_DECL, 4)

// jfloat GetFloatField(JNIEnv *env, jobject obj, jfieldID fieldID);
DEFINE_MEMBER(GetFloatField, VAR_DECL, 3)
// void SetFloatField(JNIEnv *env, jobject obj, jfieldID fieldID, jfloat val);
DEFINE_MEMBER(SetFloatField, VAR_DECL, 4)
// jfloat GetStaticFloatField(JNIEnv *env, jclass clazz, jfieldID fieldID);
DEFINE_MEMBER(GetStaticFloatField, VAR_DECL, 3)
// void SetStaticFloatField(JNIEnv *env, jclass clazz, jfieldID fieldID, jfloat value);
DEFINE_MEMBER(SetStaticFloatField, VAR_DECL, 4)

// jdouble GetDoubleField(JNIEnv *env, jobject obj, jfieldID fieldID);
DEFINE_MEMBER(GetDoubleField, VAR_DECL, 3)
// void SetDoubleField(JNIEnv *env, jobject obj, jfieldID fieldID, jdouble val);
DEFINE_MEMBER(SetDoubleField, VAR_DECL, 4)
// jdouble GetStaticDoubleField(JNIEnv *env, jclass clazz, jfieldID fieldID);
DEFINE_MEMBER(GetStaticDoubleField, VAR_DECL, 3)
// void SetStaticDoubleField(JNIEnv *env, jclass clazz, jfieldID fieldID, jdouble value);
DEFINE_MEMBER(SetStaticDoubleField, VAR_DECL, 4)

// void GetBooleanArrayRegion(JNIEnv *env, jbooleanArray array, jsize start, jsize len, jboolean *buf);
DEFINE_MEMBER(GetBooleanArrayRegion, VAR_DECL, 5)
// void SetBooleanArrayRegion(JNIEnv *env, jbooleanArray array, jsize start, jsize len, const jboolean *buf);
DEFINE_MEMBER(SetBooleanArrayRegion, VAR_DECL, 5)

// void GetByteArrayRegion(JNIEnv *env, jbyteArray array, jsize start, jsize len, jbyte *buf);
DEFINE_MEMBER(GetByteArrayRegion, VAR_DECL, 5)
// void SetByteArrayRegion(JNIEnv *env, jbyteArray array, jsize start, jsize len, const jbyte *buf);
DEFINE_MEMBER(SetByteArrayRegion, VAR_DECL, 5)

// void GetCharArrayRegion(JNIEnv *env, jcharArray array, jsize start, jsize len, jchar *buf);
DEFINE_MEMBER(GetCharArrayRegion, VAR_DECL, 5)
// void SetCharArrayRegion(JNIEnv *env, jcharArray array, jsize start, jsize len, const jchar *buf);
DEFINE_MEMBER(SetCharArrayRegion, VAR_DECL, 5)

// void GetShortArrayRegion(JNIEnv *env, jshortArray array, jsize start, jsize len, jshort *buf);
DEFINE_MEMBER(GetShortArrayRegion, VAR_DECL, 5)
// void SetShortArrayRegion(JNIEnv *env, jshortArray array, jsize start, jsize len, const jshort *buf);
DEFINE_MEMBER(SetShortArrayRegion, VAR_DECL, 5)

// void GetIntArrayRegion(JNIEnv *env, jintArray array, jsize start, jsize len, jint *buf);
DEFINE_MEMBER(GetIntArrayRegion, VAR_DECL, 5)
// void SetIntArrayRegion(JNIEnv *env, jintArray array, jsize start, jsize len, const jint *buf);
DEFINE_MEMBER(SetIntArrayRegion, VAR_DECL, 5)

// void GetLongArrayRegion(JNIEnv *env, jlongArray array, jsize start, jsize len, jlong *buf);
DEFINE_MEMBER(GetLongArrayRegion, VAR_DECL, 5)
// void SetLongArrayRegion(JNIEnv *env, jlongArray array, jsize start, jsize len, const jlong *buf);
DEFINE_MEMBER(SetLongArrayRegion, VAR_DECL, 5)

// void GetFloatArrayRegion(JNIEnv *env, jfloatArray array, jsize start, jsize len, jfloat *buf);
DEFINE_MEMBER(GetFloatArrayRegion, VAR_DECL, 5)
// void SetFloatArrayRegion(JNIEnv *env, jfloatArray array, jsize start, jsize len, const jfloat *buf);
DEFINE_MEMBER(SetFloatArrayRegion, VAR_DECL, 5)

// void GetDoubleArrayRegion(JNIEnv *env, jdoubleArray array, jsize start, jsize len, jdouble *buf);
DEFINE_MEMBER(GetDoubleArrayRegion, VAR_DECL, 5)
// void SetDoubleArrayRegion(JNIEnv *env, jdoubleArray array, jsize start, jsize len, const jdouble *buf);
DEFINE_MEMBER(SetDoubleArrayRegion, VAR_DECL, 5)

// jobject GetObjectArrayElement(JNIEnv *env, jarray array, jsize index);
DEFINE_MEMBER(GetObjectArrayElement, VAR_DECL, 3)

// void SetObjectArrayElement(JNIEnv *env, jarray array, jsize index, jobject value);
DEFINE_MEMBER(SetObjectArrayElement, VAR_DECL, 4)

#endif // DEFINE_MEMBER
