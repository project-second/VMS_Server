# ONVIF gSOAP

ONVIF SOAP 코드는 직접 작성하지 않고 gSOAP으로 생성한다.

## 디렉터리

- `wsdl/`: ONVIF WSDL/XSD 입력 파일
- `generated/`: `wsdl2h`, `soapcpp2` 생성 산출물

## 생성

ONVIF WSDL/XSD 파일을 `third_party/onvif/wsdl`에 둔 뒤 프로젝트 루트에서 실행한다.

```bash
cmake -S . -B build -G Ninja
cmake --build build --target onvif-gsoap-generate
cmake -S . -B build -G Ninja
cmake --build build
```

생성 후 `generated/` 아래의 `.h`, `.cpp`, `.c`, `.nsmap` 파일을 함께 커밋한다.

## 빌드 target

CMake target 이름은 `onvif-gsoap`이다. 생성된 소스가 있으면 static library로 빌드되고, 아직 생성된 소스가 없으면 include path만 제공하는 interface target으로 남는다.
