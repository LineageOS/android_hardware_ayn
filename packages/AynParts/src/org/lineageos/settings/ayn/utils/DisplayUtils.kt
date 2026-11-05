/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.ayn.utils

import android.content.Context
import android.hardware.display.DisplayManager
import android.hardware.input.InputManager
import android.view.Display

object DisplayUtils {

    const val CONTROLLER_DESCRIPTOR = "90dfbbe0bbbab74642c53cae4ae0aa640d40ec21"

    private val Context.displayManager: DisplayManager?
        get() = getSystemService(DisplayManager::class.java)

    private val Context.inputManager: InputManager?
        get() = getSystemService(InputManager::class.java)

    private fun internalDisplays(context: Context): List<Display> =
        context.displayManager?.displays.orEmpty().filter { it.type == Display.TYPE_INTERNAL }

    fun isDualScreen(context: Context): Boolean = internalDisplays(context).size >= 2

    private fun getDualScreenIds(context: Context): List<String> =
        internalDisplays(context).take(2).map { it.uniqueId ?: it.displayId.toString() }

    private fun bindInputDevice(context: Context, descriptor: String, displayId: String) {
        context.inputManager?.addUniqueIdAssociationByDescriptor(descriptor, displayId)
    }

    private fun unbindInputDevice(context: Context, descriptor: String) {
        context.inputManager?.removeUniqueIdAssociationByDescriptor(descriptor)
    }

    fun setInputFocus(context: Context, value: String) {
        val displayIds = getDualScreenIds(context)

        val targetDisplay =
            when (value) {
                "primary" -> displayIds.getOrNull(0)
                "secondary" -> displayIds.getOrNull(1)
                else -> null
            }

        if (targetDisplay != null) {
            bindInputDevice(context, CONTROLLER_DESCRIPTOR, targetDisplay)
        } else {
            unbindInputDevice(context, CONTROLLER_DESCRIPTOR)
        }
    }
}
