# PAQ 分层识别与领域专家融合：修正版完整设计

日期：2026-08-31  
状态：设计完成；本轮只读源码和公开资料，未修改源码、未编译、未运行压缩实验。

## 1. 结论

直接把识别到的数据交给 OpenZL、NNCP、JPEG XL、FLAC、FFV1 等完整编码器，不应成为主架构。这样会带来误路由、多个不一致的 bitstream、额外依赖、元数据无法逐字节复原、速度不可控等问题。

正确方向是学习这些领先方案的机制，把它们拆成两类 PAQ 原生能力：

1. **可逆表示专家**：字段拆流、转置、delta、字典、通道去相关、空间/时间预测等，把隐藏结构显式化；
2. **概率专家**：文本神经预测、数值分箱上下文、图像邻域、音频残差、视频空间/时间上下文等，把概率送入现有 mixer；

最终仍由同一个 PAQ mixer 系统和算术编码器产生归档。外部完整 codec 只保留为很后期、极少数、能够严格恢复原 bitstream 的例外路线，不是默认设计。

整个系统必须是分层的，而且每一层都保留真实的残余 `DEFAULT`：

```text
输入字节
├─ 已知格式/容器（强证据）
│  └─ 解析内部同质区域
│     ├─ 子类型 A -> 可逆配方 + PAQ 专家集 A
│     ├─ 子类型 B -> 可逆配方 + PAQ 专家集 B
│     └─ 该格式内未知区域 -> DEFAULT_RESIDUAL
└─ 原 DEFAULT
   └─ 二次结构识别
      ├─ 结构/数值/宽文本/日志/图像/音频等候选
      │  └─ 再细分 -> 可逆配方 + PAQ 专家集
      └─ 小、稀有、模糊、随机、加密、无法验证 -> DEFAULT_RESIDUAL
```

这里的“路由”只决定启用哪套 PAQ 原生配方和专家，不等于调用另一个压缩程序。

## 2. 为什么直接外部路由会出问题

### 2.1 识别正确，不代表可以恢复原文件字节

把 PNG 解码成像素再编码为 JPEG XL、把 H.264 解码成帧再编码为 FFV1、把 FLAC 解码成 PCM 再编码，都可以保持像素/帧/采样，但通常不能恢复原 PNG/H.264/FLAC bitstream。本项目的无损标准是：

```text
decode(archive(input_bytes)) == input_bytes
```

因此，只有两类操作天然合格：

- 对原字节做有严格逆变换的表示变换，并记录完整参数；
- 只改变概率预测，不改变被编码的原字节序列。

### 2.2 误路由会造成比例和速度退化

扩展名、单个 magic、熵或某一段的周期性都可能误判。若误判后启动完整外部 codec，成本高且无法由 PAQ mixer 自适应纠正。若它只是一个概率专家，mixer 可以逐步降低其权重；若它是通用双射变换，至少不会损坏数据，但仍可能让压缩率变差。

### 2.3 完整双路线比较过慢

运行一次原 PAQ、再运行一个或多个候选 codec，确实可以选择最小输出，但代价接近多次完整压缩。主路径不采用这种方法。公开榜和将来的离线 oracle 用来训练“哪类数据启用什么配方”，运行时只做一次有界识别和一次 PAQ 编码。

必须诚实说明：不做双编码，就不能数学保证每个独立文件都不比原 PAQ 大。可以保证的是逐字节无损；比例目标是通过严格拒识、离线验证、尺寸分桶、轻量代价估计和 mixer 自适应，把平均退化风险压低。

## 3. 分类与识别必须分开

“文件类型”和“压缩专家类型”不是一回事。建议引入三个不同概念：

| 概念 | 回答的问题 | 例子 |
|---|---|---|
| `FormatId` | 这些字节按什么格式组织，边界在哪里？ | WAV、TAR、JPEG、SAO、未知 |
| `ProfileId` | 当前同质负载有什么统计/语义结构？ | PCM16_STEREO、FIXED_RECORD、XML_MARKUP、FLOAT_COLUMN |
| `RecipeId + ExpertSet` | 采用什么可逆节点和概率专家？ | MID_SIDE+LPC、FIELD_SPLIT+DELTA、TEXT_LM |

同一个格式可含多个 profile。例如 WAV 有头、metadata、PCM；TAR 有头和不同成员；PDF/Office/ZIP 内可包含文本、图像、压缩流。相反，同一个 `FLOAT_COLUMN` profile 也可能来自 SAO、Parquet、科学数组或裸二进制。

### 3.1 四级识别器

#### L0：边界与容器识别

优先使用内部签名、固定偏移字段、长度关系、校验和、版本和结束标志。扩展名只能作为候选提示，不能单独触发变换。PRONOM/DROID 的经验也将扩展名列为最低可信、二进制签名更可靠、容器内签名最可靠。

输出必须包含精确的区域边界：header、payload、padding、trailer、嵌套成员。解析不完整或尺寸越界立即拒识，不允许“尽量猜”。

#### L1：已知格式内部解析

由格式解析器把容器拆成同质区域。例如：

- `WAV -> RIFF chunks + PCM frames + padding`；
- `TAR -> 512-byte headers + member payloads + padding`；
- `SAO -> header + star-record table -> six typed fields`；
- `JPEG -> markers + coefficient/token contexts`；
- `PE/ELF -> headers + code/data/relocation sections`。

非 `DEFAULT` 也必须继续走这一层细分，不能把 `TEXT`、`AUDIO`、`IMAGE` 当终点。

#### L2：DEFAULT 二次结构发现

对无格式或未识别负载生成有限候选：固定记录 stride、元素宽度与端序、二维 row width、UTF-16/32 lane、日志模板、文本/源码、邻域相关、通道交错等。

候选必须在至少两个不重叠窗口中一致。对 32 KiB 块可以一次扫描完整块；大块只取固定预算的首/中/尾窗口。单一 H0 熵、单一重复率或单一最佳 lag 不能直接决定变换。

#### L3：配方选择

识别器不选择外部 codec，而是输出一个版本化的 PAQ 配方：

```text
RecognitionResult {
  exact byte range
  FormatId / ProfileId
  schema parameters
  evidence tier
  RecipeId
  ExpertSet mask
  residual fallback
}
```

编码器把已选择的配方和参数写入归档；解码器只执行记录的逆配方，绝不重新分类。

### 3.2 证据等级决定允许动作

| 等级 | 必须满足 | 允许动作 | 失败行为 |
|---|---|---|---|
| `E3 PARSED_EXACT` | 完整格式/容器不变量成立，边界闭合 | 语义字段拆流、通用双射变换、专用概率专家 | 任一不变量失败即拒识 |
| `E2 STRUCTURE_STABLE` | 多窗口一致的 stride/width/endian/row 证据 | 参数写入归档的通用双射变换 + 专家 | 分数/边际不足回 DEFAULT |
| `E1 MODEL_HINT` | 只有文本性、局部相关、token 等软证据 | 不改字节顺序，只加可降权概率专家 | 低置信度不启用 |
| `E0 UNKNOWN` | 证据冲突、短、稀有、随机、加密 | 原 `ContextModelGeneric` | 终止分类 |

即使 E2 误判，`transpose`、模整数 delta、byte shuffle 等节点也必须对所有参数合法输入保持双射，因此不影响无损；E3 的语义 parser 则必须 fail-closed，不能在半解析状态下变换。

### 3.3 小文件策略

以下阈值是实现起点，后续需离线校准，不是本轮实验结论：

- `<4 KiB`：只有 E3 格式识别可触发；否则直接 DEFAULT；
- `4–16 KiB`：允许 E3 和低开销 E1 模型，不做高开销字段图；
- `16–64 KiB`：可完整单次扫描并启用 E2；32 KiB Silesia 属于此桶；
- `>64 KiB`：固定预算首/中/尾采样，解析器仍可流式验证；
- 重型文本 LM 另设更高的最小尺寸，短文本使用轻量 context-mixing/LSTM 专家。

## 4. PAQ 内部怎样“学习它们的思想”

### 4.1 表示层：可逆 Transform Graph

把当前单个 `StructuredDataFilter` 扩展成小型、版本化、可逆 DAG。节点只负责显式化结构，不自己产生外部格式：

- `SplitStruct`：AoS 固定记录拆成 SoA 字段流；
- `SplitContainer`：header/payload/padding/trailer 分区；
- `Transpose/ByteShuffle/BitPlane`：让相同意义的位或字节相邻；
- `Delta/Delta2/LookbackDelta/Zigzag`：数值残差；
- `Dictionary/Tokenize`：词典与 index 分流；
- `ChannelDecorrelate`：left/right、mid/side、颜色可逆变换；
- `SpatialResidual/TemporalResidual/LPCResidual`：整数可逆预测残差；
- `TemplateSplit`：日志模板、变量、时间戳分流；
- `ConcatRecipe`：精确恢复原字段顺序、分隔符、padding 和未知区域。

每个叶子流仍交给 PAQ，不交给 OpenZL/FSE/ANS/FLAC/FFV1。

### 4.2 概率层：统一 Expert 接口

领域模型统一实现：

```text
predict(bit_context) -> p(1)
update(actual_bit)
reset(stream_metadata)
```

`ExpertSet` 决定当前流启用哪些模型。基础 Match/Normal/Record 等保留，专用专家作为额外输入进入现有 mixer；离线证据充分时才允许禁用明显无关且昂贵的模型。

概率专家预测错误不会破坏解码，因为编码器和解码器按同样历史更新；它只改变码长。mixer 可根据近期误差降低坏专家权重，这比整个外部 codec 的硬路由安全。

### 4.3 控制层：有界门控

每个 profile 固定：最大节点数、最大叶子流数、最大专家数、内存预算、最小块长和允许的 recipe。运行时只在这个小集合内用直方图偏斜、delta 方差、字典重复率、预测残差等廉价量选择一个分支，不做无界搜索或多次完整压缩。

这正是 OpenZL 最值得借鉴的部分：离线训练计划，运行时只读轻量统计并记录已选路径；不是把 OpenZL 当黑盒子调用。

## 5. 公开结果提供的机制证据

| 领域/公开结果 | 可确认的事实 | 应迁移到 PAQ 的思想 | 不能据此声称什么 |
|---|---|---|---|
| Silesia | `paq8px_v215 -12L` 总计 27,825,511 B，说明 PAQ 是强通用骨干；`precomp|cmix v21` 在 `webster/xml/x-ray` 局部更好 | 保留残余 PAQ；按子类增强文本/XML/数值 | 不能说一种专家全局替代 PAQ |
| OpenZL SAO | 官方 SAO 为 3,516,649 B；核心是 header/table 分离、六字段拆流，SRA0 delta、SDEC0 transpose、低基数字段 tokenize | `SplitStruct + per-field recipe + PAQ field experts` | 不直接生成 OpenZL bitstream；不同机器/配置的榜单不能当严格同条件证明 |
| Large Text Benchmark | 2026-08-30：Transformer/NNCP/CMIX 结果显著小于表中的 PAQ8PX，但时间和内存很高 | 字典/文本预处理、神经字节概率、深层 context mixing | 不能把所有 32 KiB 短文本交给 Transformer |
| Pco | 数值序列采用 mode -> latent、delta/lookback、bin + exact offset | 数值拆 latent，bin-id 与 offset 分流，再由 PAQ 编码 | 缺少同语料 PAQ 比较，属于候选机制 |
| CLP | 日志拆成 log type、变量、时间戳；变量再分词典/非词典 | 模板流、字典变量流、数值/时间流，各自 PAQ 专家 | 不代表任意 JSON/文本都应按日志处理 |
| JPEG XL Modular | palette、可逆颜色变换、self-correcting predictor、上下文树；还支持 legacy JPEG 精确重建 | 原始栅格的可逆颜色/空间专家；JPEG 系数/marker 上下文 | raw-pixel 榜不能证明原 PNG/JPEG bitstream 可直接替换 |
| FLAC RFC 9639 | 通道去相关、fixed/LPC predictor、partitioned Rice residual、verbatim fallback | PCM 的 channel/LPC/residual 表示与概率上下文 | 已压缩 FLAC 不能靠解码重编码恢复原 bitstream |
| FFV1 RFC 9043 | plane/slice、`median(l,t,l+t-tl)` 预测、邻域差分上下文 | 原始视频/栅格的空间预测与上下文 | 旧视频榜不能证明替代任意 PAQ 或 H.264 文件 |
| Sprintz | 在线预测、bit packing、zero-run，面向小块多变量整数时序 | 轻量时序 predictor、residual bit-width/run 上下文 | 量化数据实验不自动适用于任意原始文件 |

公开榜在这里用于选择“值得移植的机制”，不是运行时让所有 codec 比赛。任何第三方字典、预训练权重和 decoder 资源都必须计入体积与内存；否则比较不公平。

## 6. 完整领域分类与专家矩阵

### 6.1 残余 DEFAULT

适用：未知、小、稀有、混合、随机、加密、解析失败、证据冲突。  
处理：完全保留当前 `ContextModelGeneric` 路径，不加 recipe 开销。  
意义：这是正常终点，不是分类失败需要强行消灭的垃圾桶。

### 6.2 固定记录与 SAO

识别：已知 SAO schema 用 E3；未知记录必须有跨窗口稳定 stride、字段 lane 分布和足够行数才到 E2。  
细分：字段按整数/浮点位型、近单调、低基数 enum、字典值、opaque bytes 再分类。  
融合：

- SAO 先分 header 与记录表，再拆六字段；
- SRA0 用 delta/单调数值上下文；
- SDEC0 用 byte transpose/range/high-byte 上下文；
- IS/MAG/XRPM/XDPM 用 tokenize，词典和 index 分开 PAQ；
- 未识别字段逐字节作为该记录内 `DEFAULT_RESIDUAL`。

这比当前“整条记录 transpose 后仍交给 Generic”更接近 OpenZL 真正的优势来源。

### 6.3 数值、列式数据、科学数组、时序

识别：优先从格式元数据取得 dtype、宽度、端序、shape、null bitmap；裸流只允许 E2 通用变换。  
子类：integer monotonic、integer bounded、float smooth、float repeated exponent、multichannel time-series、bitmask/enum。  
融合：

- Pco 式 `mode -> latent`，如整数倍数/小余数分离；
- delta、delta2、lookback delta、XOR-float、zigzag；
- bin-id 与 exact-offset 分流，PAQ 分别建模；
- byte shuffle/bitplane；二维数组再用 vertical/Lorenzo/median 候选；
- null、dictionary index、value 分流。

Parquet/ORC 中已经压缩的 page 不应解码重编码；只有能精确保留原 page bitstream 时才解析，否则使用 bitstream 模型或 DEFAULT。

### 6.4 自然语言、XML、源码、宽文本

识别：UTF 合法性、BOM、跨窗口文本比例、XML/JSON grammar 完整性、源码 lexer 一致性；扩展名只提示。  
子类：natural-language、XML markup/text、JSON keys/values/numbers、source tokens/comments/whitespace、UTF-16/32。  
融合：

- NNCP/CMIX 思路作为 deterministic byte/token probability expert，概率进入 mixer；
- 字典或词形变换必须同时保存大小写、空白、标点、escape 和未知 token；
- XML/JSON 拆 grammar/key/value/number streams；解析失败处回文本或 DEFAULT；
- 源码拆 token class、identifier、literal、comment、whitespace；
- UTF-16/32 先 lane split，再按文本专家处理有效 code-unit 流。

32 KiB 用轻量专家；大型文本才允许重型 LM。预训练权重必须内置并计入发布体积，或改用在线确定性训练。

### 6.5 日志

识别：多行、稳定 delimiter、时间戳位置、重复模板、变量槽在多窗口一致。  
融合：借鉴 CLP，把每行拆成 template-id、dictionary variables、numeric variables、timestamp delta 和原始 delimiter/escape 流；每流使用文本/字典/数值 PAQ 专家。无法稳定抽取模板时回普通文本，不把所有 JSON 当日志。

### 6.6 原始图像与 JPEG/PNG 等 bitstream

识别：BMP/PNM/TIFF/DICOM 等解析到精确 pixel region、width、height、channel、bit depth、stride/padding。  
融合：可逆 YCoCg-R/通道差、palette、plane split、JPEG XL 式 weighted/self-correcting spatial predictor、FFV1 median predictor、预测误差上下文，最终仍 PAQ 编码。

JPEG 使用现有 coefficient/marker 模型并吸收更强的系数邻域与上下文思想；PNG/zlib 只在现有 exact transform 可逆时展开。已经压缩的图片不能仅因“图像类”就解码为像素。JPEG XL 的 legacy-JPEG 精确重建可作为 P3 例外研究，但不是主线。

### 6.7 PCM 音频与已压缩音频

识别：RIFF/WAVE、AIFF 等必须验证 chunk 边界、sample format、channels、block alignment 和 padding。  
融合：header/metadata 原样流；PCM 按 channel split，尝试 left/right、mid/side，FLAC 式 fixed predictor/LPC residual、zigzag/bitplane；音频残差概率进入 PAQ mixer。全部预测采用有定义宽度的整数运算。

FLAC/ALAC/MP3 等原 bitstream 只做格式语法模型或 DEFAULT，不解码后重编码。

### 6.8 原始视频与压缩视频

识别：只有 raw YUV、无压缩 AVI 或可精确定位的 raw frame 才能进入像素路线。  
融合：frame/plane split，FFV1 median 空间预测，前帧/运动为零阶的轻量时间预测，预测残差/邻域差分上下文，PAQ 编码。H.264/HEVC/AV1 等只做 NAL/语法上下文或 DEFAULT，不能转成 FFV1 后宣称原文件无损。

### 6.9 TAR、ZIP、Office、PDF 等容器

TAR 不需要一个“TAR 压缩专家”，而要递归解析成员：header、member payload、padding，各成员重新进入完整识别树。ZIP/Office/PDF 同理；central directory、对象顺序、metadata、未知字段和压缩 bitstream 必须保留。只在可证明 bit-exact 逆变换的成员上展开，否则把该成员作为 residual bitstream。

### 6.10 可执行文件

现有 EXE filter 继续保留，并细分 PE/ELF、架构、code/data/relocation/import sections。学习可逆 call/jump normalization、地址/immediate 分流、opcode/operand 上下文；未知节回 DEFAULT。格式解析必须验证所有 section offset 和 length。

### 6.11 基因组、图、数据库记录

- FASTA/FASTQ/SAM：identifier、base、quality、position/CIGAR/variant 字段分流；gzip/BAM/CRAM wrapper 若不能逐字节恢复则不展开；
- 图邻接表：vertex boundary、degree、sorted neighbor delta、reference/interval/run contexts；只有明确图格式或完整文本 grammar 才变换；
- DBF/CSV/row-store：schema/column split、dictionary/RLE/delta/bitpacking 思路进入 PAQ；自由文本列仍走文本专家。

## 7. 速度与风险控制

1. **只解析一次**：边界解析结果同时供 block splitting、profile 和 recipe 使用；
2. **不跑完整候选 codec**：运行时没有 PAQ/OpenZL/NNCP 两路完整比较；
3. **固定预算**：统计识别最多读取固定窗口，DAG/streams/experts 都有上限；
4. **按尺寸门控**：重型模型只在足够大的同质流启用；
5. **每流重置/共享明确**：避免一个字段污染另一个字段的上下文；
6. **坏概率专家可降权**：专用模型只是 mixer 输入；
7. **变换 fail-closed**：metadata 非法、计算溢出、边界不闭合即 DEFAULT；
8. **recipe 版本化**：解码器验证 node id、参数范围、总输出长度、递归深度和内存上限；
9. **旧路径不变**：`DEFAULT_RESIDUAL` 直接使用原 Generic，便于回退和对照。

## 8. 对当前代码的具体映射

当前代码已经有正确的局部脚手架，但接法不足：

- `src/filter/Filters.hpp` 目前只在 `selectedType == DEFAULT` 时调用 `detectDefaultStructure()`。应改成所有已识别 terminal block 都进入 `recognizeProfile()`，从而实现“非 DEFAULT 再细分”；
- `src/filter/DefaultStructureDetector.hpp` 当前只在 RECORD/NUMERIC/WIDE_TEXT 三族中硬选一个。应拆成签名/parser registry、profile detectors、evidence tiers 和显式 `DEFAULT_RESIDUAL`；
- `src/filter/StructuredDataFilter.hpp` 当前主要是一进一出的 transpose/delta/shuffle。应扩展为多流 reversible graph，支持字段级不同配方；
- `src/model/ContextModel.cpp` 当前 RECORD/NUMERIC/WIDE_TEXT 最终落入 `default` 分支的 `ContextModelGeneric`。应由 `ExpertSetFactory(ProfileId, stream-role)` 建立专用 mixer 配置；
- `src/model/ContextModelGeneric.cpp` 已经展示了多个模型进同一 mixer 的结构。新的 Pco/CLP/JXL/FLAC/FFV1 思想应按这一接口添加，而不是包完整 codec；
- `src/BlockType.hpp` 不宜继续为每个细分类型无限增加枚举。建议追加一个版本化 `PROFILED`/`EXPERT_GRAPH` frame，内部携带 `ProfileId + RecipeId + stream descriptors`，保留 v216 的旧 ID；
- 解码端读取 frame recipe 后执行逆 DAG，再拼回原字节；不包含识别器和训练器。

## 9. 实施优先级

### P0：架构骨架

- 分离 `FormatId/ProfileId/RecipeId`；
- 建立分层识别 registry、证据等级和残余 DEFAULT；
- 建立 `ExpertSet` 门控和 reversible graph wire format；
- 加入递归、流数、内存、尺寸与 parser 安全限制。

### P1：最能验证思想的四条路线

1. SAO 六字段图，而不是整记录 transpose；
2. XML/自然语言轻量概率专家，吸收 CMIX/NNCP 思路；
3. typed numeric 的 Pco 式 latent/delta/bin-offset 分流；
4. UTF-16/32 lane + 文本专家。

这四条覆盖用户已观察到的结构、文本、数值和剩余 DEFAULT 问题，也能验证共同框架。

### P2：扩展高价值领域

- CLP 式日志；
- PCM/FLAC 式预测残差；
- raw raster 的 JXL/FFV1 式空间专家；
- TAR/ZIP/Office/PDF 递归成员；
- PE/ELF 细分。

### P3：复杂或高风险领域

- raw video 时间专家；
- genomics、graph、Parquet/ORC page；
- legacy-JPEG 的 JXL exact reconstruction 例外；
- 任何完整外部 codec 后端。

## 10. 最终判断

可以学习这些领先方案的思想，而且这比直接路由更适合 PAQ。核心不是建立“PAQ 或其他 codec”的竞赛器，而是把领先方案分解成可逆表示节点和概率专家，再由 PAQ 统一混合、统一编码、统一解码。

分类识别也不能是一层标签。它必须是“容器边界 -> 内部 profile -> 子类型/schema -> recipe/expert”，并且每一层都能拒识并回到残余 `DEFAULT`。这样才能同时覆盖 SAO、文本、数值、图像、音频、视频、TAR 以及未来未见类型，而不因强行分类破坏系统。

本轮没有做任何源码改动、编译或压缩实验；这里只完成了下一轮实现所需的架构设计和公开机制筛选。
