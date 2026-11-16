# Cornix 전용 동글 ZMK 설정

이 저장소는 원래 Cornix 키맵을 유지하면서 Cornix 양쪽과 nice!nano 호환 OLED 동글만 빌드합니다. Corne 빌드나 원래 Corne 저장소는 포함하지 않습니다. 원래 Cornix 저장소는 별도로 보존합니다.

- `cornix_dongle_oled.uf2`: OLED 동글용. 기존 `DongleDongleCorn`, Bongo Cat, macOS 수정키, L/R 배터리 화면을 유지합니다.
- `cornix_left_for_dongle.uf2`: Cornix 왼쪽, 동글의 주변기기.
- `cornix_right_for_dongle.uf2`: Cornix 오른쪽, 동글의 주변기기.
- `cornix_halves_settings_reset.uf2`: Cornix 양쪽의 설정 초기화용. 각 반쪽에 따로 넣습니다.
- `dongle_nicenano_settings_reset.uf2`: nice!nano 호환 동글 설정 초기화용.

설정 초기화는 페어링과 호스트 프로필을 지웁니다. 필요 없이 설정 초기화를 하지 마세요. 기존 동글 구성에서 옮길 때 먼저 새 일반 UF2만 사용해 연결을 확인하고, 연결되지 않을 경우에만 기존 저장된 연결 관계를 고려해 필요한 장치들을 초기화하고 재페어링합니다. 초기화하면 Corne 같은 기존 주변기기 및 호스트 연결을 다시 잡아야 할 수 있습니다. 물리 기기에서 입력, 각 인코더, OLED 및 USB/BLE를 직접 검증하세요.

`config/cornix.keymap`은 사용자 Cornix 원본에서 가져온 파일이며, `config/cornix_dongle_adapter.keymap`은 이를 동글 빌드에서 사용합니다. 화면 모듈은 이 저장소에 포함되어 있으며 L/R 배터리와 Bongo Cat 등의 수정사항을 유지합니다.

## 화면 모듈 출처

`boards/shields/dongle_display`는 [englmaxi/zmk-dongle-display](https://github.com/englmaxi/zmk-dongle-display)를 기반으로 사용자가 수정한 코드입니다. 해당 디렉터리의 MIT LICENSE를 참조하세요. 화면 모듈을 별도 사용자 저장소에서 내려받지 않습니다.

동글 없이 쓰는 복구용으로 `cornix_left_standalone.uf2`도 별도 빌드합니다. 자동 전환이 아니며 역할을 바꿀 때 설정 초기화와 재페어링이 필요할 수 있습니다.
