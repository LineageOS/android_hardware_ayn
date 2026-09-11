/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.ayn.utils

import android.content.Context
import android.content.Intent
import android.hardware.input.InputManager
import android.os.Bundle

object RemappingUtils {
    private const val CONTROLLER_DESCRIPTOR = "dc4619eefd76423d152d1b3789814fb245dfd008"

    fun launchGamepadRemapping(context: Context) {
        val inputManager = context.getSystemService(InputManager::class.java) ?: return
        val device = inputManager.getInputDeviceByDescriptor(CONTROLLER_DESCRIPTOR) ?: return
        val args = Bundle().apply { putParcelable("input_device_identifier", device.identifier) }

        val intent =
            Intent().apply {
                setClassName("com.android.settings", "com.android.settings.SubSettings")
                putExtra(
                    ":settings:show_fragment",
                    "com.android.settings.input.gamecontroller.GameControllerFragment",
                )
                putExtra(":settings:show_fragment_args", args)
                putExtra(":settings:show_fragment_title", device.name)
            }

        context.startActivity(intent)
    }
}
