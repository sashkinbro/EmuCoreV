package com.sbro.emucorev.core

enum class GpuVendor {
    ADRENO,
    MALI,
    POWERVR,
    XCLIPSE,
    UNKNOWN
}

data class GpuModelInfo(
    val vendor: GpuVendor,
    val displayName: String,
    val adrenoFamily: AdrenoFamily? = null
)
