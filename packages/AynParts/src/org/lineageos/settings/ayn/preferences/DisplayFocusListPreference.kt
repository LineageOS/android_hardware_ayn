/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.ayn.preferences

import android.content.Context
import android.util.AttributeSet
import androidx.preference.ListPreference
import androidx.preference.PreferenceManager
import org.lineageos.settings.ayn.utils.DisplayUtils

class DisplayFocusListPreference
@JvmOverloads
constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = androidx.preference.R.attr.dialogPreferenceStyle,
) : ListPreference(context, attrs, defStyleAttr) {

    override fun onAttached() {
        super.onAttached()
        isVisible = DisplayUtils.isDualScreen(context)
    }

    override fun setValue(value: String?) {
        super.setValue(value)
        DisplayUtils.setInputFocus(context, value.orEmpty())
    }

    companion object {
        private const val KEY_GAMEPAD_DISPLAY_FOCUS = "gamepad_display_focus"

        fun restoreOnBoot(context: Context) {
            val sharedPrefs = PreferenceManager.getDefaultSharedPreferences(context)
            val storedValue = sharedPrefs.getString(KEY_GAMEPAD_DISPLAY_FOCUS, "free").orEmpty()
            DisplayUtils.setInputFocus(context, storedValue)
        }
    }
}
