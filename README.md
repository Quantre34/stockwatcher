# StockWatcher

Zara Türkiye ürün takip aracı. Belirlediğin anahtar kelimelere ve beden/renk filtrelerine göre Zara'yı otomatik tarar, eşleşen ürün bulunduğunda bildirim verir.

## Özellikler

- Zara TR kategori listesinden kategori seçimi
- Zara sayfa URL'si yapıştırarak doğrudan kategori tarama (indirim/editorial sayfaları dahil)
- Anahtar kelime, beden ve renk filtreleri
- İndirimli ürünleri kırmızı renk ile vurgulama
- macOS bildirimi ve ses çıkışı

## Kurulum (macOS)

**Gereksinimler:** Xcode Command Line Tools, libcurl (macOS ile birlikte gelir)

```bash
git clone https://github.com/Quantre34/stockwatcher.git
cd stockwatcher
make
sudo mkdir -p /usr/local/bin
sudo make install
```

Kurulumdan sonra terminalde `stockwatcher` yazarak başlatabilirsin.

## Kullanım

```
stockwatcher
```

Başlatınca:
1. Kategori seçim yöntemi seç:
   - **1 — Listeden seç:** Bölüm (Kadın/Erkek/Çocuk...) ve kategori seç
   - **2 — URL yapıştır:** Zara'da herhangi bir sayfayı aç, adres çubuğundaki URL'yi yapıştır
2. Anahtar kelime ekle (örn: `fitilli`, `tişört`)
3. Tarama başlar — eşleşen ürün bulunduğunda bildirim gelir

### Komutlar (çalışırken)

| Komut | Açıklama |
|---|---|
| `add <kelime>` | Anahtar kelime ekle |
| `rm <kelime>` | Anahtar kelime sil |
| `list` | Kelimeleri listele |
| `size XS S M` | Beden filtresi koy |
| `color Siyah` | Renk filtresi koy |
| `filters` | Aktif filtreleri göster |
| `pause` / `resume` | Taramayı duraklat / devam ettir |
| `status` | Durum bilgisi |
| `quit` | Çıkış |

## İndirim Sayfaları

Zara'nın indirim ve editorial koleksiyon sayfaları standart kategori listesinde görünmez. Bu sayfaları taramak için:

1. Başlatırken **"2 — URL yapıştır"** seçeneğini seç
2. Zara'da indirim sayfasını aç (örn: `zara.com/tr/tr/s-woman-editorial-ou8989.html?v1=2440352`)
3. Adres çubuğundaki URL'yi yapıştır

Program `v1=` parametresini otomatik olarak okur.
