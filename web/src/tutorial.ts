import type { Lang } from './i18n';
import type { Layer } from './map';
import type { Tool } from './MapCanvas';
export type TutorialSetup = {
  layer: Layer;
  tool: Tool;
  color?: number;
  clear?: boolean;
  yaml?: boolean;
};
const steps: { target: string; setup: TutorialSetup }[] = [
  { target: '.canvas-wrap', setup: { layer: 'map', tool: 'pen' } },
  { target: '.swatches', setup: { layer: 'map', tool: 'pen' } },
  {
    target: '[data-tour="tool-pen"]',
    setup: { layer: 'map', tool: 'pen', color: 0 },
  },
  { target: '.brush-control', setup: { layer: 'map', tool: 'pen', color: 0 } },
  { target: '[data-tour="picker"]', setup: { layer: 'map', tool: 'picker' } },
  {
    target: '[data-tour="tool-line"]',
    setup: { layer: 'map', tool: 'line', color: 0 },
  },
  {
    target: '[data-tour="tool-rect"]',
    setup: { layer: 'map', tool: 'rect', color: 0 },
  },
  {
    target: '[data-tour="tool-polygon"]',
    setup: { layer: 'map', tool: 'polygon', color: 0 },
  },
  {
    target: '[data-tour="tool-fill"]',
    setup: { layer: 'map', tool: 'fill', color: 255 },
  },
  { target: '[data-tour="undo"]', setup: { layer: 'map', tool: 'pen' } },
  { target: '.zoom-control', setup: { layer: 'map', tool: 'hand' } },
  {
    target: '[data-tour="layer-keepout"]',
    setup: { layer: 'keepout', tool: 'polygon' },
  },
  {
    target: '[data-tour="speed-value"]',
    setup: { layer: 'speed', tool: 'rect' },
  },
  {
    target: '[data-tour="mask-display"]',
    setup: { layer: 'speed', tool: 'rect' },
  },
  {
    target: '[data-tour="mask-clear"]',
    setup: { layer: 'keepout', tool: 'rect', clear: true },
  },
  {
    target: '[data-tour="tool-select"]',
    setup: { layer: 'keepout', tool: 'select' },
  },
  {
    target: '[data-tour="layer-rotate"]',
    setup: { layer: 'map', tool: 'rotate' },
  },
  { target: '[data-tour="layer-crop"]', setup: { layer: 'map', tool: 'crop' } },
  {
    target: '[data-tour="layer-origin"]',
    setup: { layer: 'map', tool: 'origin' },
  },
  { target: '[data-tour="metadata"]', setup: { layer: 'map', tool: 'pen' } },
  {
    target: '[data-tour="yaml"]',
    setup: { layer: 'map', tool: 'pen', yaml: true },
  },
  { target: '.document-name-input', setup: { layer: 'map', tool: 'pen' } },
  { target: '.save-button', setup: { layer: 'map', tool: 'pen' } },
  { target: '[data-tour="download"]', setup: { layer: 'map', tool: 'pen' } },
  { target: '.account-trigger', setup: { layer: 'map', tool: 'pen' } },
];
const ko = [
  [
    '예제 지도',
    '원래 제공하던 예제 지도를 열었습니다. 안내에 따라 직접 편집해 보세요. 실습 내용은 내 지도나 공유 워크스페이스에 저장되지 않습니다.',
  ],
  [
    '세 가지 색',
    '흰색은 이동 가능한 공간, 검정은 장애물, 회색은 미확인 영역입니다. 원본 픽셀은 자동으로 바꾸지 않으며 점유 판정은 YAML 설정을 따릅니다.',
  ],
  [
    '펜',
    '지도 위를 드래그해 그려 보세요. 흰색을 선택하면 장애물을 지우고 빈 공간으로 만들 수 있습니다.',
  ],
  [
    '굵기',
    '슬라이더를 움직이거나 px 값을 입력하세요. 옆의 짧은 선에서 선택한 굵기를 확인할 수 있습니다.',
  ],
  [
    '스포이드',
    '지도에서 색을 클릭하면 실제 PGM 픽셀값을 가져옵니다. 원본의 회색이 기본 회색과 다를 때 사용하세요.',
  ],
  ['직선', '시작점에서 끝점까지 드래그하세요. 선택한 색과 굵기로 직선을 그립니다.'],
  ['박스', '대각선 방향으로 드래그해 사각형을 만드세요. 내부를 선택한 색으로 채웁니다.'],
  [
    '영역',
    '점 네 개를 찍고 Enter, 우클릭, 또는 완료 버튼으로 닫아 보세요. Backspace는 마지막 점을 지우고 Esc는 취소합니다.',
  ],
  [
    '배경 채우기',
    '배경을 클릭하면 같은 픽셀값으로 연결된 영역을 채웁니다. 열린 틈이 있으면 바깥까지 이어질 수 있습니다.',
  ],
  [
    '실행 취소',
    '왼쪽 화살표로 직전 작업을 취소하고 옆 화살표로 다시 실행하세요. Ctrl/Cmd+Z와 Ctrl/Cmd+Shift+Z도 사용할 수 있습니다.',
  ],
  [
    '이동과 확대',
    '드래그로 화면을 옮기고 휠로 확대하세요. 다른 도구에서도 Space+드래그나 가운데 버튼으로 이동할 수 있습니다. F는 화면 맞춤, 0은 100%입니다.',
  ],
  [
    '진입 금지 영역',
    '이제 원본 위의 마스크에 그립니다. 점을 찍어 금지 영역을 완성해 보세요. 빨간색은 화면 표시용이며 원본 지도는 바뀌지 않습니다.',
  ],
  [
    '속도 영역',
    '속도 비율을 정한 뒤 박스를 그려 보세요. 30%는 로봇 최대 속도의 30%입니다. 값을 바꿔도 이미 그린 영역은 그대로입니다.',
  ],
  [
    '마스크 표시',
    '체크박스로 표시를 끄거나 불투명도를 조절해 원본과 비교하세요. 숨겼거나 불투명도가 0인 레이어에는 그릴 수 없습니다.',
  ],
  [
    '제한 해제',
    '기존 금지 영역 위로 박스를 그려 제한을 지워 보세요. 펜과 다각형으로도 같은 작업을 할 수 있습니다.',
  ],
  [
    '구역 하나 삭제',
    '그려 놓은 금지 영역을 클릭한 뒤 Delete 또는 휴지통 버튼을 누르세요. 같은 값으로 이어진 픽셀은 하나의 구역으로 선택됩니다.',
  ],
  [
    '회전',
    '지도 위를 드래그하거나 오른쪽 슬라이더와 숫자로 각도를 바꾸세요. 적용 또는 Enter로 확정하고 Esc로 취소합니다. 마스크도 함께 회전합니다.',
  ],
  [
    '자르기',
    '남길 영역을 드래그한 뒤 모서리나 변을 잡아 조절하세요. 안쪽을 끌면 선택 영역이 움직입니다. 적용하면 남은 영역의 월드 좌표를 유지합니다.',
  ],
  [
    '원점 이동과 방향',
    '좌표축 가운데 점을 드래그해 원점을 옮기세요. 축 끝점이나 짧은 원호를 잡으면 방향이 바뀝니다. Shift는 15° 단위로 맞추고 Enter는 적용합니다.',
  ],
  [
    '해상도와 좌표',
    '해상도는 한 픽셀의 실제 거리(m)입니다. X·Y는 위치, Yaw는 방향이며 숫자로도 수정할 수 있습니다. 변경 후 적용하세요.',
  ],
  [
    '점유 설정',
    'free_thresh와 occupied_thresh는 픽셀의 점유 판정 기준입니다. negate는 명암의 의미를 뒤집습니다. 실제 로봇 설정에 맞출 때만 변경하세요.',
  ],
  ['이름 변경', '상단 이름을 클릭해 직접 바꾸세요. Enter로 확정하고 Esc로 되돌릴 수 있습니다.'],
  [
    '저장',
    '수정하면 저장 아이콘이 활성화됩니다. 실제 지도는 Google 계정에 저장합니다. 여기서는 실습 저장만 하며 계정에는 올리지 않습니다.',
  ],
  [
    '다운로드',
    '지도와 마스크의 PGM/YAML을 ZIP으로 내려받습니다. ZIP은 다시 업로드하거나 데스크톱 앱에서 사용할 수 있습니다.',
  ],
  [
    '계정과 화면',
    '계정 아이콘에서 언어와 다크·라이트 모드를 바꿉니다. 실습을 마치면 지도 목록으로 돌아갑니다. 공유 워크스페이스는 목록 왼쪽에서 만들 수 있습니다.',
  ],
];
const en = [
  [
    'Example map',
    'The bundled example is open. Try each tool as you follow along. Practice changes are never saved to your account or a shared workspace.',
  ],
  [
    'Three colors',
    'White means free space, black means occupied, and gray means unknown. Original pixels stay intact; YAML thresholds determine occupancy.',
  ],
  ['Pen', 'Drag on the map to draw. Choose white to remove obstacles and mark free space.'],
  [
    'Brush width',
    'Move the slider or enter a pixel value. The short stroke previews your selected width.',
  ],
  [
    'Eyedropper',
    'Click a map pixel to sample its actual PGM value. Use this when the original gray differs from the default.',
  ],
  ['Line', 'Drag from the start to the endpoint. The line uses the selected color and width.'],
  ['Box', 'Drag diagonally to create a rectangle filled with the selected color.'],
  [
    'Region',
    'Place four points, then finish with Enter, right-click, or the finish button. Backspace removes the last point; Esc cancels.',
  ],
  [
    'Fill background',
    'Click to fill connected pixels with the same value. A gap in a boundary can let the fill spread outside.',
  ],
  [
    'Undo and redo',
    'Use the two arrows to undo and redo. Ctrl/Cmd+Z and Ctrl/Cmd+Shift+Z work too.',
  ],
  [
    'Pan and zoom',
    'Drag to pan and scroll to zoom. Space+drag or the middle button pans with any tool. F fits the map; 0 shows 100%.',
  ],
  [
    'Keepout region',
    'You are now drawing a mask over the original. Place points to mark a forbidden region. Red is only an overlay; map pixels stay unchanged.',
  ],
  [
    'Speed region',
    'Choose a percentage and draw a box. 30% means 30% of the robot maximum speed. Changing the slider does not alter existing regions.',
  ],
  [
    'Mask visibility',
    'Toggle visibility or adjust opacity to compare with the original. Hidden layers and layers with zero opacity cannot be painted.',
  ],
  [
    'Remove restrictions',
    'Draw a box over a keepout region to clear its restriction. The pen and polygon tools can do this too.',
  ],
  [
    'Delete one region',
    'Click a keepout region, then press Delete or use the trash button. Connected pixels with the same value form one region.',
  ],
  [
    'Rotate',
    'Drag on the map or use the angle slider and number field. Apply or Enter confirms; Esc cancels. Masks rotate with the map.',
  ],
  [
    'Crop',
    'Drag the area to keep, then adjust its edges or corners. Drag inside to move it. Applying preserves the world coordinates of retained pixels.',
  ],
  [
    'Move and rotate the origin',
    'Drag the axis center to move the origin. Drag an axis endpoint or the arc to change yaw. Shift snaps to 15°; Enter applies.',
  ],
  [
    'Resolution and coordinates',
    'Resolution is the real distance per pixel in meters. X/Y set position and Yaw sets orientation. Edit the numbers and apply.',
  ],
  [
    'Occupancy settings',
    'free_thresh and occupied_thresh classify pixels. negate reverses the grayscale meaning. Change these only to match your robot configuration.',
  ],
  [
    'Rename',
    'Click the name at the top to edit it. Enter commits; Esc restores the previous name.',
  ],
  [
    'Save',
    'The save icon becomes available after an edit. Real maps save to your Google account. This tutorial only simulates saving and never uploads your practice map.',
  ],
  [
    'Download',
    'Download map and mask PGM/YAML pairs as a ZIP. Upload the ZIP again or extract it for the desktop app.',
  ],
  [
    'Account and display',
    'Open the account menu to change language or theme. Finishing returns to your map list, where the sidebar also lets you create shared workspaces.',
  ],
];
const ja = [
  [
    'サンプル地図',
    '付属の地図を開きました。各ツールを実際に操作できます。練習内容はアカウントや共有ワークスペースに保存されません。',
  ],
  [
    '3つの色',
    '白は通行可能、黒は障害物、灰色は不明領域です。元の画素は自動変換せず、占有判定はYAMLの設定に従います。',
  ],
  ['ペン', '地図上をドラッグして描きます。白を選ぶと障害物を消して通行可能な領域にできます。'],
  ['太さ', 'スライダーかpx値で太さを変えます。短い線で選択中の太さを確認できます。'],
  [
    'スポイト',
    '画素をクリックすると実際のPGM値を取得します。元の灰色が標準色と異なる場合に使います。',
  ],
  ['直線', '始点から終点へドラッグします。選択した色と太さで直線を描きます。'],
  ['四角形', '対角方向にドラッグすると、選択した色で塗られた四角形ができます。'],
  [
    '領域',
    '4点を置き、Enter・右クリック・完了ボタンで閉じます。Backspaceで最後の点を削除、Escで中止します。',
  ],
  [
    '背景の塗りつぶし',
    '同じ画素値でつながった領域をクリックして塗ります。境界に隙間があると外側にも広がります。',
  ],
  [
    '元に戻す・やり直す',
    '2つの矢印で操作を戻したりやり直せます。Ctrl/Cmd+ZとCtrl/Cmd+Shift+Zも使えます。',
  ],
  [
    '移動と拡大',
    'ドラッグで移動、ホイールで拡大します。他のツールでもSpace+ドラッグや中央ボタンで移動できます。Fで全体表示、0で100%です。',
  ],
  [
    '進入禁止領域',
    '元の地図に重ねたマスクへ描きます。点を置いて禁止領域を作ってみましょう。赤は表示用で、元の画素は変わりません。',
  ],
  [
    '速度領域',
    '割合を選び、四角形を描きます。30%はロボットの最大速度の30%です。値を変えても既存領域は変化しません。',
  ],
  [
    'マスク表示',
    '表示の切替や不透明度で元の地図と比較できます。非表示や不透明度0のレイヤーには描けません。',
  ],
  ['制限解除', '進入禁止領域に四角形を重ねて制限を解除します。ペンや多角形でも消せます。'],
  [
    '領域を1つ削除',
    '禁止領域をクリックし、Deleteかごみ箱ボタンで削除します。同じ値でつながった画素を1つの領域として選択します。',
  ],
  [
    '回転',
    '地図をドラッグするか、角度スライダーや数値で調整します。適用・Enterで確定、Escで取消。マスクも一緒に回転します。',
  ],
  [
    '切り抜き',
    '残す範囲をドラッグし、辺や角で調整します。内側のドラッグで移動できます。残る画素のワールド座標は維持されます。',
  ],
  [
    '原点の移動と向き',
    '座標軸の中心をドラッグすると原点が移動します。軸の端や円弧で向きを変えます。Shiftで15°刻み、Enterで適用します。',
  ],
  [
    '解像度と座標',
    '解像度は1画素の実距離(m)です。X/Yは位置、Yawは向きです。数値を編集し、適用してください。',
  ],
  [
    '占有設定',
    'free_threshとoccupied_threshで画素を分類し、negateで明暗の意味を反転します。ロボットの設定に合わせる場合に変更します。',
  ],
  ['名前の変更', '上部の名前をクリックして編集します。Enterで確定、Escで元に戻せます。'],
  [
    '保存',
    '編集後に保存アイコンが有効になります。通常はGoogleアカウントへ保存しますが、この練習では保存を再現するだけでアップロードしません。',
  ],
  [
    'ダウンロード',
    '地図とマスクのPGM/YAMLをZIPで保存します。再アップロードやデスクトップ版で使用できます。',
  ],
  [
    'アカウントと表示',
    'アカウントメニューで言語とテーマを変更します。完了すると地図一覧に戻り、左側から共有ワークスペースも作れます。',
  ],
];
const zh = [
  ['示例地图', '已打开自带的示例地图。可按步骤实际操作。练习内容不会保存到账号或共享工作区。'],
  [
    '三种颜色',
    '白色表示可通行，黑色表示障碍物，灰色表示未知区域。原始像素不会自动转换，占用判定遵循YAML设置。',
  ],
  ['画笔', '在地图上拖动即可绘制。选择白色可移除障碍物，将其改为可通行区域。'],
  ['粗细', '拖动滑块或输入px值。旁边的短线用于预览所选粗细。'],
  ['吸管', '点击地图像素以获取实际PGM值。原图灰色与默认值不同时可使用此工具。'],
  ['直线', '从起点拖到终点，以所选颜色和粗细绘制直线。'],
  ['矩形', '沿对角线拖动，创建由所选颜色填充的矩形。'],
  ['区域', '点击四个点后，用Enter、右键或完成按钮闭合。Backspace撤销最后一点，Esc取消。'],
  ['填充背景', '点击以填充具有相同像素值的连通区域。边界存在缺口时，填充可能扩散到外部。'],
  ['撤销与重做', '使用两个箭头撤销和重做，也可使用Ctrl/Cmd+Z和Ctrl/Cmd+Shift+Z。'],
  [
    '移动与缩放',
    '拖动平移，滚轮缩放。其他工具中也可用Space+拖动或中键平移。F适应窗口，0显示100%。',
  ],
  [
    '禁行区域',
    '现在绘制的是原图上方的遮罩。点击各点完成禁行区域。红色仅用于显示，不会改变原始地图像素。',
  ],
  [
    '限速区域',
    '先选择百分比，再绘制矩形。30%表示机器人最大速度的30%。更改数值不会改变已绘制的区域。',
  ],
  ['遮罩显示', '切换显示或调整不透明度，以便与原图比较。隐藏或不透明度为0的图层不能绘制。'],
  ['解除限制', '在禁行区域上绘制矩形即可解除限制，也可使用画笔或多边形工具。'],
  ['删除单个区域', '点击禁行区域后，按Delete或点击垃圾桶。同值连通像素会作为一个区域选择。'],
  ['旋转', '拖动地图，或使用角度滑块与数值输入。应用或Enter确认，Esc取消。遮罩会随地图一起旋转。'],
  ['裁剪', '拖动要保留的范围，再调整边或角。拖动内部可移动选区。应用后保留像素的世界坐标不变。'],
  [
    '移动与旋转原点',
    '拖动坐标轴中心可移动原点，拖动轴端点或圆弧可改变方向。Shift按15°对齐，Enter应用。',
  ],
  ['分辨率与坐标', '分辨率是每像素的实际距离(m)。X/Y设置位置，Yaw设置方向。修改数值后应用。'],
  [
    '占用设置',
    'free_thresh和occupied_thresh用于分类像素，negate反转灰度含义。仅在匹配机器人配置时修改。',
  ],
  ['重命名', '点击顶部名称直接编辑。Enter确认，Esc恢复原名。'],
  ['保存', '编辑后保存图标才可用。实际地图保存到Google账号，本教程仅模拟保存，不会上传练习地图。'],
  ['下载', '将地图和遮罩的PGM/YAML对下载为ZIP。可重新上传，或解压后用于桌面版。'],
  ['账号与显示', '在账号菜单中更改语言和主题。完成后返回地图列表，也可从左侧创建共享工作区。'],
];
export function tutorialSteps(lang: Lang) {
  const copy = { ko, en, ja, zh }[lang];
  return steps.map((step, i) => ({
    ...step,
    title: copy[i][0],
    body: copy[i][1],
  }));
}
export const tutorialLabels = {
  ko: {
    name: '튜토리얼',
    previous: '이전',
    next: '다음',
    finish: '실습 마치기',
    close: '튜토리얼 종료',
    saved: '저장 동작을 연습했습니다. 계정에는 업로드하지 않았습니다.',
  },
  en: {
    name: 'Tutorial',
    previous: 'Previous',
    next: 'Next',
    finish: 'Finish practice',
    close: 'End tutorial',
    saved: 'Save simulated. Nothing was uploaded to your account.',
  },
  ja: {
    name: 'チュートリアル',
    previous: '前へ',
    next: '次へ',
    finish: '練習を終了',
    close: 'チュートリアル終了',
    saved: '練習内容を保存しました。アカウントにはアップロードしていません。',
  },
  zh: {
    name: '教程',
    previous: '上一步',
    next: '下一步',
    finish: '完成练习',
    close: '结束教程',
    saved: '练习已保存，未上传到账号。',
  },
};
