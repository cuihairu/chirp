package chirp.mobile

import android.content.Context
import android.widget.EditText
import androidx.core.content.ContextCompat

/** Small styled single-line text field for the shell's inline dialogs. */
fun textField(context: Context, hint: String, value: String): EditText =
    EditText(context).apply {
        this.hint = hint
        this.setText(value)
        setTextColor(ContextCompat.getColor(context, R.color.chirp_text_primary))
        setHintTextColor(ContextCompat.getColor(context, R.color.chirp_text_tertiary))
        background = ContextCompat.getDrawable(context, R.drawable.bg_input)
        val pad = (12 * context.resources.displayMetrics.density).toInt()
        setPadding(pad, pad, pad, pad)
        singleLine = true
    }
