package chirp.mobile;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

/**
 * Hue-hash letter avatar (design language): rounded square filled with
 * hsl(h, 42%, 46%) where h comes from a stable hash of the label, the
 * label's first character centered in white. Same label always renders the
 * same color within this client; the recipe matches the approved prototype
 * avatar spec.
 *
 * Java, not Kotlin: layout-referenced custom views must resolve during the
 * javac pass (view-binding gencode compiles before the built-in Kotlin
 * output exists on AGP 9), so shell classes used inside XML layouts stay in
 * src/app/java; Kotlin callers bind them as usual.
 */
public class AvatarView extends View {

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint glyph = new Paint(Paint.ANTI_ALIAS_FLAG);
    private String label = "";

    public AvatarView(Context context) {
        this(context, null);
    }

    public AvatarView(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public AvatarView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        glyph.setColor(Color.WHITE);
        glyph.setTextAlign(Paint.Align.CENTER);
    }

    /** Bind an identity label (user id or group id); idempotent per label. */
    public void bind(String name) {
        if (name == null || name.equals(label)) {
            return;
        }
        label = name;
        int h = ((name.hashCode() % 360) + 360) % 360;
        // hsl(42%, 46%) → hsv: chroma c = (1 - |2l-1|)·s, v = l + c/2,
        // sv = c/v (exact conversion, no eyeballing).
        float l = 0.46f;
        float s = 0.42f;
        float c = (1f - Math.abs(2f * l - 1f)) * s;
        float v = l + c / 2f;
        fill.setColor(Color.HSVToColor(new float[]{h, c / v, v}));
        setContentDescription(name);
        invalidate();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        glyph.setTextSize(h * 0.42f);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (getWidth() == 0 || getHeight() == 0) {
            return;
        }
        // Design row radius (12) on the row avatar size (44), scaled to any
        // size the view is laid out at.
        float radius = getHeight() * (12f / 44f);
        canvas.drawRoundRect(
                new RectF(0, 0, getWidth(), getHeight()), radius, radius, fill);
        if (label.isEmpty()) {
            return;
        }
        String first = label.substring(0, 1).toUpperCase(java.util.Locale.ROOT);
        float baseline = getHeight() / 2f - (glyph.descent() + glyph.ascent()) / 2f;
        canvas.drawText(first, getWidth() / 2f, baseline, glyph);
    }
}
