package com.mcp.mobileuse;

public class JniBridge {
    static {
        System.loadLibrary("mcp_mobile_use_jni");
    }

    public static native int nativeStart(int port, String backend);

    public static native void nativeStop();
}
