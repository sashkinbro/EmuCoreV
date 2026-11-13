package com.sbro.emucorev.core.input

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Window
import java.lang.reflect.Proxy
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class RearTouchRoutingTest {
    private fun mouse(action: Int, buttons: Int, relative: Boolean = false, device: Int = 1): MotionEvent {
        val properties = MotionEvent.PointerProperties().apply { id=0; toolType=MotionEvent.TOOL_TYPE_MOUSE }
        val coords = MotionEvent.PointerCoords().apply { x=10f; y=20f }
        return MotionEvent.obtain(1,2,action,1,arrayOf(properties),arrayOf(coords),0,buttons,1f,1f,device,0,
            if (relative) InputDevice.SOURCE_MOUSE_RELATIVE else InputDevice.SOURCE_MOUSE,0)
    }
    private fun input(events: MutableList<Boolean>) = RearTouchInput({ _,down,_,_->events+=down }, {})
        .apply { configure(RearTouchProfile(listOf(RearTouchPoint(0,bindings=listOf(RearTouchBinding(RearBindingKind.MOUSE,MotionEvent.BUTTON_SECONDARY)))))); setActive(true) }

    @Test fun rearMouseBindingPreservesUnboundMoveAndWheelForExistingPipeline() {
        val events=mutableListOf<Boolean>();val input=input(events)
        for(action in listOf(MotionEvent.ACTION_MOVE,MotionEvent.ACTION_SCROLL)) {
            val original=mouse(action,MotionEvent.BUTTON_PRIMARY)
            val result=input.motion(original,1000,600)
            assertFalse(result.consumed)
            assertEquals(MotionEvent.BUTTON_PRIMARY,result.event!!.buttonState)
            assertEquals(action,result.event.actionMasked)
            result.event.recycle();original.recycle()
        }
        assertTrue(events.isEmpty())
    }

    @Test fun selectedMouseDownAndUpAreConsumedWithoutSyntheticLeftClick() {
        val events=mutableListOf<Boolean>();val input=input(events)
        val down=mouse(MotionEvent.ACTION_DOWN,MotionEvent.BUTTON_SECONDARY)
        val up=mouse(MotionEvent.ACTION_UP,0)
        assertTrue(input.motion(down,1000,600).consumed)
        assertTrue(input.motion(up,1000,600).consumed)
        assertEquals(listOf(true,false),events)
        down.recycle();up.recycle()
    }

    @Test fun relativeCapturedMouseUsesTheSameRearBindingAndKeepsUnboundAim() {
        val events=mutableListOf<Boolean>();val input=input(events)
        val down=mouse(MotionEvent.ACTION_DOWN,MotionEvent.BUTTON_SECONDARY,true)
        val move=mouse(MotionEvent.ACTION_MOVE,MotionEvent.BUTTON_SECONDARY,true)
        val up=mouse(MotionEvent.ACTION_UP,0,true)
        assertTrue(input.motion(down,1000,600).consumed)
        val result=input.motion(move,1000,600)
        assertFalse(result.consumed);assertEquals(0,result.event!!.buttonState)
        assertTrue(input.motion(up,1000,600).consumed)
        assertEquals(listOf(true,false),events)
        result.event.recycle();down.recycle();move.recycle();up.recycle()
    }

    @Test fun focusedDialogWindowCapturesKeyWithoutActivityDispatch() {
        val input=RearTouchInput({_,_,_,_->}, {})
        var forwarded=0;var binding:RearTouchBinding?=null
        val delegate=Proxy.newProxyInstance(Window.Callback::class.java.classLoader,arrayOf(Window.Callback::class.java)) { _,method,_->
            if(method.name=="dispatchKeyEvent") forwarded++
            if(method.returnType==Boolean::class.javaPrimitiveType) false else null
        } as Window.Callback
        input.capture={binding=it}
        val callback=RearBindingWindowCallback(delegate,input) {1000 to 600}
        assertTrue(callback.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_R2)))
        assertEquals(KeyEvent.KEYCODE_BUTTON_R2,binding!!.code)
        assertEquals(0,forwarded)
        assertNull(input.capture)
    }

    @Test fun globalAndPerGameProfilesRoundTripIndependently() {
        val repository=RearTouchProfileRepository(RuntimeEnvironment.getApplication())
        val global=RearTouchProfile(listOf(RearTouchPoint(0,x=.2f,screenEnabled=true)))
        val game=RearTouchProfile(listOf(RearTouchPoint(0,x=.8f,bindings=listOf(RearTouchBinding(RearBindingKind.KEY,KeyEvent.KEYCODE_BUTTON_L2)))))
        repository.saveGlobal(global);repository.saveGame("PCSB00244",game)
        assertEquals(.2f,repository.effective("PCSF00036").points[0].x)
        assertEquals(.8f,repository.effective("pcsb00244").points[0].x)
        assertEquals(KeyEvent.KEYCODE_BUTTON_L2,repository.effective("PCSB00244").points[0].bindings.single().code)
        repository.inherit("PCSB00244")
        assertEquals(.2f,repository.effective("PCSB00244").points[0].x)
        assertNull(RearTouchProfileRepository.decode("{\"version\":99,\"points\":[]}"))
    }

    @Test fun menuReleaseDoesNotConsumeTheNextUnboundMouseRelease() {
        val input=input(mutableListOf())
        val rearDown=mouse(MotionEvent.ACTION_DOWN,MotionEvent.BUTTON_SECONDARY)
        assertTrue(input.motion(rearDown,1000,600).consumed)
        input.setActive(false)
        val rearUp=mouse(MotionEvent.ACTION_UP,0)
        input.motion(rearUp,1000,600).event?.recycle()
        input.setActive(true)
        val leftDown=mouse(MotionEvent.ACTION_DOWN,MotionEvent.BUTTON_PRIMARY)
        val leftUp=mouse(MotionEvent.ACTION_UP,0)
        val down=input.motion(leftDown,1000,600);assertFalse(down.consumed);down.event?.recycle()
        val up=input.motion(leftUp,1000,600);assertFalse(up.consumed);up.event?.recycle()
        rearDown.recycle();rearUp.recycle();leftDown.recycle();leftUp.recycle()
    }

    @Test fun disconnectClearsOnlyItsMouseStreamAndPreservesOtherDeviceHold() {
        val events=mutableListOf<Boolean>();val input=input(events)
        val first=mouse(MotionEvent.ACTION_DOWN,MotionEvent.BUTTON_SECONDARY,device=1)
        val second=mouse(MotionEvent.ACTION_DOWN,MotionEvent.BUTTON_SECONDARY,device=2)
        assertTrue(input.motion(first,1000,600).consumed);assertTrue(input.motion(second,1000,600).consumed)
        input.removeDevice(1)
        assertEquals(listOf(true),events)
        val leftDown=mouse(MotionEvent.ACTION_DOWN,MotionEvent.BUTTON_PRIMARY,device=1)
        val leftUp=mouse(MotionEvent.ACTION_UP,0,device=1)
        input.motion(leftDown,1000,600).event?.recycle()
        val unboundUp=input.motion(leftUp,1000,600);assertFalse(unboundUp.consumed);unboundUp.event?.recycle()
        val secondUp=mouse(MotionEvent.ACTION_UP,0,device=2)
        assertTrue(input.motion(secondUp,1000,600).consumed)
        assertEquals(listOf(true,false),events)
        first.recycle();second.recycle();leftDown.recycle();leftUp.recycle();secondUp.recycle()
    }

    @Test fun cancellationWithoutOldUpDoesNotOwnTheNextMouseGesture() {
        val input=input(mutableListOf())
        val rearDown=mouse(MotionEvent.ACTION_DOWN,MotionEvent.BUTTON_SECONDARY)
        assertTrue(input.motion(rearDown,1000,600).consumed)
        input.releaseAll() // Focus loss can discard the old pointer stream without an UP.
        val leftDown=mouse(MotionEvent.ACTION_DOWN,MotionEvent.BUTTON_PRIMARY)
        val leftUp=mouse(MotionEvent.ACTION_UP,0)
        val down=input.motion(leftDown,1000,600);assertFalse(down.consumed);down.event?.recycle()
        val up=input.motion(leftUp,1000,600);assertFalse(up.consumed);up.event?.recycle()
        rearDown.recycle();leftDown.recycle();leftUp.recycle()
    }
}
