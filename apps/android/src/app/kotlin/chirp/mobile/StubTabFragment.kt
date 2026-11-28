package chirp.mobile

import android.os.Bundle
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment

/**
 * Interim content for the tabs whose screens land in later commits of this
 * redesign (好友/组队/语音/设备). An honest 建设中 state — never a fake
 * plane; replaced by the real fragment in its own commit.
 */
class StubTabFragment : Fragment() {

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        val text = TextView(requireContext())
        text.setText(R.string.tab_stub)
        text.gravity = Gravity.CENTER
        text.textSize = 13f
        text.setTextColor(ContextCompat.getColor(requireContext(), R.color.chirp_text_tertiary))
        return text
    }
}
