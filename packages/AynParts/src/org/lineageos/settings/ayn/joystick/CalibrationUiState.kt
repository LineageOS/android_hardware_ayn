/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.ayn.joystick

sealed class CalibrationPhase {
    data object Intro : CalibrationPhase()

    data object Center : CalibrationPhase()

    data object RangeLeft : CalibrationPhase()

    data object RangeRight : CalibrationPhase()

    data object DeadzoneLeft : CalibrationPhase()

    data object DeadzoneRight : CalibrationPhase()

    data object TriggerLeft : CalibrationPhase()

    data object TriggerRight : CalibrationPhase()

    data object Test : CalibrationPhase()
}

data class CalibrationUiState(
    val phase: CalibrationPhase = CalibrationPhase.Intro,
    val readyToAdvance: Boolean = false,
    val currentSample: JoystickSample? = null,
    val calibrationData: CalibrationData? = null,
    val mappedLeftX: Float = 0f,
    val mappedLeftY: Float = 0f,
    val mappedRightX: Float = 0f,
    val mappedRightY: Float = 0f,
    val mappedLeftTrigger: Float = 0f,
    val mappedRightTrigger: Float = 0f,
    val rawLeftX: Float = 0f,
    val rawLeftY: Float = 0f,
    val rawRightX: Float = 0f,
    val rawRightY: Float = 0f,
    val rawLeftTrigger: Float = 0f,
    val rawRightTrigger: Float = 0f,
)
