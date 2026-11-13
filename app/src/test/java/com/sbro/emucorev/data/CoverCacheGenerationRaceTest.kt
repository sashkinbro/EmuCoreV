package com.sbro.emucorev.data

import java.nio.file.Files
import java.security.MessageDigest
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.*
import org.junit.Assert.*
import org.junit.Test

class CoverCacheGenerationRaceTest {
    @Test fun expiredOldMissCannotRemoveFreshPostClearMiss() = runBlocking {
        val root=Files.createTempDirectory("vita-cover-review-").toFile()
        val clock=AtomicLong(1L)
        val blockNextClock=AtomicBoolean(false)
        val oldReadEntered=CountDownLatch(1)
        val releaseOldRead=CountDownLatch(1)
        val calls=AtomicInteger()
        val hash=MessageDigest.getInstance("SHA-256").digest("fixture".toByteArray()).joinToString("") { "%02x".format(it) }
        val asset=VitaCoverAsset(42,VitaCoverStyle.FLAT,"a".repeat(40),"covers/42.webp",hash)
        val cache=CoverDiskCache(root,{_,_,_->calls.incrementAndGet();CoverFetchResult.NOT_FOUND},{true},now={
            val captured=clock.get()
            if(blockNextClock.compareAndSet(true,false)) {
                oldReadEntered.countDown()
                check(releaseOldRead.await(2,TimeUnit.SECONDS))
            }
            captured
        })
        try {
            withTimeout(4000) {
                assertNull(cache.resolve(asset));assertEquals(1,calls.get())
                clock.set(31*60*1000L)
                blockNextClock.set(true)
                val oldOwner=async(Dispatchers.Default){cache.resolve(asset)}
                assertTrue(oldReadEntered.await(2,TimeUnit.SECONDS))
                cache.clear()
                assertNull(cache.resolve(asset));assertEquals(2,calls.get())
                releaseOldRead.countDown();assertNull(oldOwner.await())
                assertNull(cache.resolve(asset))
                assertEquals("Fresh post-clear confirmed miss must suppress the next fetch",2,calls.get())
            }
        } finally {releaseOldRead.countDown();root.deleteRecursively()}
    }
}
