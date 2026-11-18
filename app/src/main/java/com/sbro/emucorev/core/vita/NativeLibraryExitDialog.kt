package com.sbro.emucorev.core.vita

import android.app.Dialog
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.Window
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.common.ImmersiveMode

/** Small, static console-style modal. No continuously animated Android overlay. */
internal class NativeLibraryExitDialog(context: Context, onExit: () -> Unit) : Dialog(context) {
    init {
        requestWindowFeature(Window.FEATURE_NO_TITLE)
        fun dp(value: Int) = (value * context.resources.displayMetrics.density).toInt()
        fun panel(top: Int, bottom: Int, radius: Int) = GradientDrawable(
            GradientDrawable.Orientation.TOP_BOTTOM, intArrayOf(top, bottom)
        ).apply {
            cornerRadius = dp(radius).toFloat()
            setStroke(dp(1), 0xff8bd8ff.toInt())
        }
        val content = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(28), dp(22), dp(28), dp(22))
            background = panel(0xf22c719c.toInt(), 0xf20b294d.toInt(), 22)
        }
        content.addView(TextView(context).apply {
            setText(R.string.native_library_exit_title)
            setTextColor(Color.WHITE)
            textSize = 24f
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
        })
        content.addView(TextView(context).apply {
            setText(R.string.emulation_exit_confirm_body)
            setTextColor(Color.WHITE)
            textSize = 17f
            gravity = Gravity.CENTER
            setPadding(0, dp(18), 0, dp(24))
        })
        val actions = LinearLayout(context).apply { orientation = LinearLayout.HORIZONTAL }
        fun action(label: Int, click: () -> Unit) = Button(context).apply {
            setText(label)
            isAllCaps = false
            setTextColor(Color.WHITE)
            textSize = 18f
            background = android.graphics.drawable.StateListDrawable().apply {
                addState(intArrayOf(android.R.attr.state_pressed), panel(0xff84dcff.toInt(), 0xff2876ac.toInt(), 28))
                addState(intArrayOf(android.R.attr.state_focused), panel(0xff6bbff1.toInt(), 0xff236da1.toInt(), 28))
                addState(intArrayOf(), panel(0xff428bb9.toInt(), 0xff163d68.toInt(), 28))
            }
            setOnClickListener { click() }
        }
        val exit = action(R.string.emulation_exit_confirm_action) { dismiss(); onExit() }
        val cancel = action(android.R.string.cancel) { dismiss() }
        actions.addView(exit, LinearLayout.LayoutParams(0, dp(52), 1f).apply { marginEnd = dp(10) })
        actions.addView(cancel, LinearLayout.LayoutParams(0, dp(52), 1f))
        content.addView(actions)
        setContentView(content)
        setCanceledOnTouchOutside(false)
        setOnShowListener {
            window?.apply {
                setBackgroundDrawableResource(android.R.color.transparent)
                setLayout(minOf(dp(600), (context.resources.displayMetrics.widthPixels * 0.82f).toInt()), WindowManager.LayoutParams.WRAP_CONTENT)
                addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
                setDimAmount(0.45f)
                ImmersiveMode.applyToDialog(this)
            }
            cancel.requestFocus()
        }
    }
}
