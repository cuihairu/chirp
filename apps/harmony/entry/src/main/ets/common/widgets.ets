import router from '@ohos.router';
import { C, R, S, avatarColor, avatarInitial } from './Tokens';

/**
 * 页面共用小件（token 体系收敛面）：圆形头像、顶栏、底部页签。四个主页面
 * （Sessions/Friends/PartyVoice/Devices）共用同一页签条，页签切换一律
 * replaceUrl（不叠历史，语义同移动 tab bar）。
 */

/** 头像：hue-hashed 底 + 名字首字符。 */
@Component
export struct Avatar {
  name: string = '';
  size: number = S.avatarMd;

  build() {
    Text(avatarInitial(this.name))
      .fontSize(this.size * 0.42)
      .fontWeight(FontWeight.Bold)
      .fontColor(C.text)
      .width(this.size)
      .height(this.size)
      .borderRadius(this.size / 2)
      .backgroundColor(avatarColor(this.name))
      .textAlign(TextAlign.Center)
  }
}

/**
 * 顶栏（56，token topbar）：标题 + 可选返回 + 可选右侧动作。
 * onBack 为空时不渲染返回键（主页签页）。
 */
@Component
export struct TopBar {
  title: string = '';
  subtitle: string = '';
  onBack: () => void = () => {};
  showBack: boolean = false;
  rightText: string = '';
  onRight: () => void = () => {};

  build() {
    Row({ space: 10 }) {
      if (this.showBack) {
        Text('‹')
          .fontSize(30)
          .fontColor(C.text)
          .width(34)
          .height(34)
          .textAlign(TextAlign.Center)
          .borderRadius(17)
          .backgroundColor(C.surface2)
          .onClick(() => this.onBack())
      }
      Column({ space: 2 }) {
        Text(this.title)
          .fontSize(17)
          .fontWeight(FontWeight.Bold)
          .fontColor(C.text)
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
        if (this.subtitle !== '') {
          Text(this.subtitle)
            .fontSize(12)
            .fontColor(C.text2)
            .maxLines(1)
            .textOverflow({ overflow: TextOverflow.Ellipsis })
        }
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)
      if (this.rightText !== '') {
        Text(this.rightText)
          .fontSize(14)
          .fontColor(C.accentSoft)
          .padding({ left: 12, right: 12, top: 6, bottom: 6 })
          .borderRadius(R.pill)
          .backgroundColor(C.accentDim)
          .onClick(() => this.onRight())
      }
    }
    .width('100%')
    .height(S.topbar)
    .padding({ left: 16, right: 16 })
    .backgroundColor(C.bg)
  }
}

interface TabItem {
  key: string;
  label: string;
  icon: string;
  url: string;
}

const TAB_ITEMS: TabItem[] = [
  { key: 'sessions', label: '聊天', icon: '💬', url: 'pages/SessionsPage' },
  { key: 'friends', label: '好友', icon: '👥', url: 'pages/FriendsPage' },
  { key: 'party', label: '组队', icon: '🎮', url: 'pages/PartyVoicePage' },
  { key: 'devices', label: '设备', icon: '📱', url: 'pages/DevicesPage' },
];

/** 底部页签（64，token tabbar）；当前页签高亮，其余点击 replaceUrl。 */
@Component
export struct TabBar {
  active: string = 'sessions';

  build() {
    Row() {
      ForEach(TAB_ITEMS, (item: TabItem) => {
        Column({ space: 3 }) {
          Text(item.icon)
            .fontSize(20)
            .fontColor(item.key === this.active ? C.accent : C.text3)
          Text(item.label)
            .fontSize(11)
            .fontColor(item.key === this.active ? C.accent : C.text3)
        }
        .layoutWeight(1)
        .justifyContent(FlexAlign.Center)
        .height('100%')
        .onClick(() => {
          if (item.key !== this.active) {
            router.replaceUrl({ url: item.url });
          }
        })
      }, (item: TabItem) => item.key)
    }
    .width('100%')
    .height(S.tabbar)
    .backgroundColor(C.surface)
    .border({ width: { top: 1 }, color: C.borderSoft })
  }
}

/** 胶囊主按钮（token r-pill + accent）。 */
@Component
export struct PillButton {
  label: string = '';
  enabled: boolean = true;
  destructive: boolean = false;
  onTap: () => void = () => {};

  build() {
    Text(this.label)
      .fontSize(15)
      .fontWeight(FontWeight.Medium)
      .fontColor(this.enabled ? (this.destructive ? C.danger : C.text) : C.text3)
      .textAlign(TextAlign.Center)
      .width('100%')
      .height(44)
      .borderRadius(R.pill)
      .backgroundColor(this.enabled ? (this.destructive ? C.accentDim : C.accent) : C.surface2)
      .opacity(this.enabled ? 1 : 0.6)
      .onClick(() => {
        if (this.enabled) this.onTap();
      })
  }
}

/**
 * 全屏 sheet（对齐原型「主操作弹全屏 sheet」的面）：标题 + 内容 + 底部
 * 取消/确认。内容由调用方以 @BuilderParam 注入。
 */
@Component
export struct Sheet {
  title: string = '';
  onCancel: () => void = () => {};
  @BuilderParam content: () => void = emptyBuilder;
  @BuilderParam actions: () => void = emptyBuilder;

  build() {
    Column() {
      Text(this.title)
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .fontColor(C.text)
        .margin({ top: 28, bottom: 20 })
      Column() {
        this.content()
      }
      .layoutWeight(1)
      .width('100%')
      .padding({ left: 20, right: 20 })
      Column({ space: 10 }) {
        this.actions()
      }
      .width('100%')
      .padding({ left: 20, right: 20, bottom: 28 })
    }
    .width('100%')
    .height('100%')
    .backgroundColor(C.bg)
  }
}

@Builder
function emptyBuilder() {
}
