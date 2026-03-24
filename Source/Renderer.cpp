#include <glm/fwd.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <cmath>
#include <array>
#include "Renderer.h"
#include "Image.h"
#include "RHI/Vulkan/VulkanCommandList.hpp"


namespace FeatherVK {
    namespace {
        uint32_t ClampExtentDimension(float value) {
            return static_cast<uint32_t>(std::max(1.0f, std::round(value)));
        }

        bool SameRect(const ViewportRect &lhs, const ViewportRect &rhs) {
            return lhs.x == rhs.x &&
                   lhs.y == rhs.y &&
                   lhs.width == rhs.width &&
                   lhs.height == rhs.height;
        }

        VkViewport ToViewport(const ViewportRect &rect) {
            VkViewport viewport{};
            viewport.x = rect.x;
            viewport.y = rect.y;
            viewport.width = rect.width;
            viewport.height = rect.height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            return viewport;
        }

        VkRect2D ToScissor(const ViewportRect &rect) {
            VkRect2D scissor{};
            scissor.offset = {static_cast<int32_t>(std::round(rect.x)), static_cast<int32_t>(std::round(rect.y))};
            scissor.extent = {ClampExtentDimension(rect.width), ClampExtentDimension(rect.height)};
            return scissor;
        }
    }

    Renderer::Renderer(MyWindow &window, Device &device1) : myWindow{window}, device{device1} {
        m_scenePanelRect = {
            static_cast<float>(UI_LEFT_WIDTH + UI_LEFT_WIDTH_2),
            0.0f,
            static_cast<float>(SCENE_WIDTH),
            static_cast<float>(SCENE_HEIGHT)};
        m_sceneViewportRect = m_scenePanelRect;
        m_currentCommandList = std::make_unique<RHI::VulkanCommandList>(device);

        recreateSwapChain();
        createCommandBuffers();
        loadOffscreenResources();
#ifndef RAY_TRACING
        loadShadow();
#endif
    }


    Renderer::~Renderer() {
        vkDeviceWaitIdle(device.device());
        freeCommandBuffers();
        freeShadowResources();
        freePickingResources();
        if (m_pickingRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device.device(), m_pickingRenderPass, nullptr);
            m_pickingRenderPass = VK_NULL_HANDLE;
        }
        freeOffscreenResources();
        if (m_sceneColorRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device.device(), m_sceneColorRenderPass, nullptr);
            m_sceneColorRenderPass = VK_NULL_HANDLE;
        }
    }

    VkCommandBuffer Renderer::beginFrame() {
        assert(!isFrameStarted && "Frame has already started");
        auto result = swapChain->acquireNextImage(&currentImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return nullptr;
        }

        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image");
        }

        isFrameStarted = true;

        auto commandBuffer = getCurrentCommandBuffer();

        VkCommandBufferBeginInfo commandBufferBeginInfo{};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin command buffer");
        }
        auto *vulkanCommandList = dynamic_cast<RHI::VulkanCommandList *>(m_currentCommandList.get());
        if (vulkanCommandList != nullptr) {
            vulkanCommandList->SetCommandBuffer(commandBuffer);
        }
        return commandBuffer;
    }

    void Renderer::endFrame() {
        assert(isFrameStarted && "Can not call endFrame while frame is not in progress");
        auto commandBuffer = getCurrentCommandBuffer();
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer");
        }

        auto result = swapChain->submitCommandBuffers(&commandBuffer, &currentImageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || myWindow.isWindowResized()) {
            myWindow.resetWindowResizedFlag();
            recreateSwapChain();
//            loadOffscreenResources();
//            loadShadow();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image");
        }
        isFrameStarted = false;
        currentFrameIndex = (currentFrameIndex + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;
        auto *vulkanCommandList = dynamic_cast<RHI::VulkanCommandList *>(m_currentCommandList.get());
        if (vulkanCommandList != nullptr) {
            vulkanCommandList->SetCommandBuffer(VK_NULL_HANDLE);
        }
    }

    void Renderer::recreateSwapChain() {
        auto extent = myWindow.getCurrentExtent();
        while (extent.width == 0 || extent.height == 0) {
            extent = myWindow.getCurrentExtent();
            glfwWaitEvents();
        }
        //闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳婀遍埀顒傛嚀鐎氼參宕崇壕瀣ㄤ汗闁圭儤鍨归崐鐐烘偡濠婂喚妯€鐎殿喗鎮傚浠嬵敇閻斿搫骞愰梻浣规偠閸庮垶宕曢柆宥嗗€堕柍鍝勫暟绾惧ジ鏌熼柇锕€寮炬繛鍫熺矒閺岋紕浠︾化鏇炰壕闁归绀侀幃鎴︽煙閼测晞藟闁逞屽墮绾绢參顢欓幇鐗堚拺闁硅偐鍋涢崝婊堟煙缁嬪灝鏆辨い顐㈢箻閹煎綊宕烽鐙呯床婵犵妲呴崹闈涒枍閿濆棛顩烽柟闂寸劍閻撱垽鏌涢幇闈涘箳闁告凹鍋嗙槐鎺撴綇閵婏箑纰嶅銈嗘尭閵堢鐣烽妸鈺婃晩闁稿繗鍋愭禍鐑芥⒒閸屾瑧顦﹂柟纰卞亰閹本寰勫畝鈧粈濠傘€掑锝呬壕闂佺粯渚楅崳锝呯暦閸洦鏁嗗〒姘处椤ュ牊绻濋悽闈涗沪闁搞劌鐖奸弫鍐煛娓氬洨鍔风紓鍌欑劍鐪夌紒璇叉閺岋綁骞囬崗鍝ョ泿闂侀€炲苯澧繛鍙夌矒楠炴垿濮€閵堝懐鐤€闂佺粯顨呴悧鍡樼椤栫偞鈷戠紓浣股戠亸顓㈡倵閸偄绗掓い顓炴喘瀵粙顢橀悢鍝勫及闂傚鍋勫ú锕傚礄閻熼偊鐒介柟閭﹀枓閸嬫挾鎲撮崟顒傤槬濡炪倧濡囬弫璇差嚕婵犳艾鍗抽柣鏃囨椤旀洟姊洪崜鑼帥闁哥姵鐗楅幈銊╁即閵忊檧鎷洪柣鐘叉穿鐏忔瑧绮婚敐鍥╃＜闁逞屽墯缁楃喖鍩€椤掑嫬鐏抽柡鍐ㄧ墕缁€鍐┿亜閺傛寧顫嶇憸鏃堝蓟濞戞ǚ妲堥柛妤冨仦閻忔挸顪冮妶鍌涙珖濠⒀勵殘閹广垹鈽夊锝呬壕婵炴垶鐟悞浠嬫煟椤撶偟鐒搁柡灞剧〒閳ь剨缍嗛崑鍡椕洪幘顔界厱闁崇懓鐏濋崝婊呪偓鍨緲鐎氫即鐛崶顒夋晣闁绘劖澹嗙槐锕傛⒒閸屾瑧顦﹂柟璇х磿缁瑩骞嬮敂鑺ユ珖闂佹寧娲栭崐鎼佹儗濡ゅ懏鐓曢柍鈺佸暟閳洟鏌ｉ幘璺烘灈妤犵偞鐗犻獮鏍敇閻愬吀鍖栫紓鍌欑椤﹂潧顭囪濠€浣糕攽閻樿宸ユ俊顐ｎ殜閹繝骞囬悧鍫濃偓鐢告⒒閸喓鈼ら柛瀣ㄥ灪椤ㄣ儵鎮欓幖顓犲姺缂備浇椴哥敮鎺曠亽闂備礁鐏濋鍛搭敂閻戞绡€闁汇垽娼ф禒鈺呮煙濞茶绨界紒杈╁仱閸┾偓妞ゆ巻鍋撻柍瑙勫灴椤㈡瑩宕崟銊ヤ壕婵°倐鍋撻柍钘夘樀閹晫绮欓崸妤€鏁归梻浣告惈濞层劑宕伴幘璇插偍闂侇剙绉甸埛鎴犵磽娴ｅ箍鈧偤骞嬮敂钘変簵闂佽法鍠撴慨鎾儗濡ゅ懏鐓曢柡鍥ュ妼閻忥繝鏌ｉ幘瀛樼闁哄苯绉堕幉鎾礋椤愩垹袘濠电偛鐡ㄧ划搴ㄥ磻閹惧鈹嶅┑鐘叉处閸婇攱銇勮箛鎾愁仱闁稿鎹囧浠嬵敃閿濆棙顔囬梻浣告贡閸庛倝銆冮崨顖氼棜鐟滅増甯楅悡娆撴⒒閸屾凹鍤熸い锔肩畵閺屾盯骞嬮悩娴嬫瀰濡ょ姷鍋為悧鐘荤嵁閺嶎収鏁囬柣鏂跨殱閺嬪繐鈹戦悙鑼憼缂侇喖绉堕崚鎺楀箻鐠囪尪鎽曢梺闈浥堥弲娑氱尵瀹ュ鐓曢柕澶堝妽绾偓绻涢崼顐㈠箺缂佺粯绻堥崺娑㈠焵椤掑嫬绀嬫い鎺嗗亾闁哥偛顦靛娲传閸曨厾鍔圭紓浣介哺濞叉绮嬮幒妤佹櫆闁绘劦鍓欓悵浼存⒑閸︻厾甯涢悽顖楁櫅椤洦鎯旈妸锔规嫽婵炶揪绲块悺鏃堝吹閸愵喗鐓曢柣妯挎珪瀹曞瞼鈧娲滈、濠囧Φ閹版澘绠抽柟鎯у帠閹綁姊绘担鑺ョ《闁哥姵鎸婚幈銊р偓闈涙憸濡垳鎲搁弮鍫濈畺鐟滅増甯掔粻鎺楁煙閻戞ê鐏ユい顒€顑嗙换婵嬪閿濆棛銆愰柣搴㈠嚬閸犳艾危閹版澘绠婚悹鍥皺閿涙粌鈹戦悙鍙夘棡閻㈩垱甯″畷銏ゆ晸閻樻枼鎷虹紓浣割儐椤戞瑩宕曢幇鐗堢厵闁告稑锕ラ崐鎰版煃閵夘垳鐣电€规洖缍婇、姘跺川椤旇偐绱﹂梻鍌欑窔閳ь剛鍋涢懟顖涙櫠閸欏绻嗘い鎰╁€曢弸娑欐叏婵犲懏顏犻柟鍙夋尦瀹曠喖妫冨☉娆愮彵闂傚倷绀侀幗婊勬叏閻㈠灚鏆滈柍銉ョ－閺嗭箓鏌熸潏鍓х暠缂佺姴顭烽弻鐔革紣娴ｅ搫濡介梺绋跨Ч濞佳団€旈崘顔嘉ч柛鈩冾殘閻熴劑姊虹粙鍖″姛缂侇噮鍨堕妴鍐ㄢ枎閹剧补鎷婚梺绋挎湰閻熝囁囬敃鍌涚厵缁炬澘宕禍鐐烘煙椤曞懎娅嶆い銏℃礋閺佸啴鍩€椤掑倻鐭嗗鑸靛姈閻撴瑩寮堕崼婵嗏挃闁伙綀浜槐鎺楁偐瀹曞洤鈷岄梺鍝勬湰濞叉繄绮诲☉姘ｅ亾閿濆簼绨撮柛瀣崌瀵挳鎮滈崱妤佹珚婵犵數濞€濞佳兾涘畝鍕哗濞寸姴顑嗛悡鐔镐繆椤栨繍鍤欑紒鑼帛閵囧嫬顕ラ弶鎸庡櫧缁炬儳銈稿鍫曞醇濞戞ê顬夌紓浣插亾閻庯綆鍋呴崣蹇撯攽閻樻彃顏悽顖涚洴閺岀喎鐣￠悧鍫濇畻閻庤娲﹂崑濠冧繆閻戣В鈧牠顢欓崫鍕瀳濡炪値浜滈崯瀛樹繆閸洖绀冮柕濞垮劚椤岸姊绘担鍛婅础闁硅櫕鎸哥叅闁靛牆顦伴崑鈺冣偓鐟板鐎氬牓寮崼婵堝姦濡炪倖甯掔€氬摜绱為弽銊х瘈濠电姴鍊绘晶鏇㈡煟閹烘垹浠涢柕鍥у楠炴帡宕卞鎯ь棜濠碉紕鍋戦崐鏍洪埡鍐濞撴埃鍋撻柕鍡曠窔瀵挳濮€閳╁啯鐝栭梻渚€鈧偛鑻晶鎵磼椤斿墽甯涚紒缁樼箓椤繈顢橀悙鏉垮闂傚倷鐒﹂幃鍫曞磿濠婂牆绀冩い蹇撶墢瀹曟粌鈹戦敍鍕杭闁稿﹥鍨垮畷鏇㈡嚑椤掍礁搴婇梺鍓插亝缁诲秴顭囬弽銊х鐎瑰壊鍠曠花鍏笺亜閵夈儳澧涚紒缁樼洴楠炲鎮欑捄渚婵＄偑鍊х徊浠嬪箹椤愶箑鐓橀柟杈剧畱閻擄繝鏌涢埄鍐︿沪缂併劎鍘ч—鍐Χ閸愩劌顬堥梺鎸庢处娴滎亝淇婄€涙鐟归柍褜鍓欓锝夘敋閳ь剙鐣烽悡搴樻斀閻庯綆浜濋弳顏堟⒒閸屾瑧顦﹂柟纰卞亜鐓ら柕濞炬櫅閻ゎ噣鏌涜椤ㄥ懐绮婚娑氱鐎瑰壊鍠曠花濂告煟閹捐泛鏋涢柡宀嬬到铻ｉ柛婵嗗缁楊參姊洪悡搴☆棌濞存粠浜璇测槈閵忕姵顥濋柣鐘充航閸斿酣宕濋鐐村€垫繛鍫濈仢閺嬬喖鏌熼鐓庘偓鍧楁偘椤旈敮鍋撻敐搴℃灍闁哄懏绮撻弻锝夋晲閸涙澘顏Δ鐘靛仦閸旀瑥顫忛搹瑙勫珰闁圭粯甯掑В鎰磽閸屾氨孝婵☆偅鐟х划瀣吋婢跺﹪鍞堕梺鍝勬川閸犲孩绂嶅Δ鍛拺缂佸娉曠粻鎶芥煃瀹勬壆澧曢柍缁樻尰缁傛帞鈧綆鍋嗛崢浠嬫⒑瑜版帒浜伴柛銊ゅ嵆閹啴鎼归崷顓狅紲闂佺粯锚濡﹪鎮℃總鍛婄厸鐎光偓閳ь剟宕伴弽顓溾偓浣糕枎閹炬潙浠奸悗鍏夊亾闁逞屽墴閹剝寰勯幇顓涙嫼闂佽崵鍠愭竟鍡涙晬瀹ュ鐓曢悗锝冨妼閳ь剚鐗楃粚杈ㄧ節閸ャ劌鈧鏌ら幁鎺戝姕婵炲懎妫涚槐鎾存媴閸︻厸妲堝銈嗗灥鐎氫即鐛€ｎ喗鍊婚柤鎭掑劗閹峰姊虹粙鎸庢拱闁煎綊绠栭崺鈧い鎺戝濡垹绱掗鑲╁缂佹鍠栭崺鈧い鎺戝瀹撲線鏌″搴″季闁轰礁鍟撮弻銊╁即濡も偓娴滃墽绱掗悙顒€鍔ょ紓宥咃躬瀵鈽夐姀鈺傛櫇闂佹寧绻傚Λ娑⑺囬妷褏纾奸柣鎰靛墮閸斻倗绱撳鍜冭含鐎殿喖顭烽崹楣冨箛娴ｅ憡鍊梺纭呭亹鐞涖儵鍩€椤掑啫鐨洪柣鏍憾濮婄粯鎷呮笟顖滃姼濡炪倖鍨靛Λ婵嬬嵁閹版澘绠瑰〒姘功缁嬪繐鈹戦悩缁樻锭妞ゆ垶鍔欏顐﹀幢濞戞瑧鍘遍柣蹇曞仜婢т粙寮弽顓熺厪濠电偟鍋撳▍鍡涙煃闁垮鐏撮柡灞剧☉閳规垿宕卞Δ濠佺磽婵＄偑鍊ら崑鎾剁不閹捐钃熸繛鎴欏灩缁秹鏌嶈閸撶喎鐣疯ぐ鎺戦敜婵°倕鍟粊锕傛⒑閸涘﹤濮﹂柛鐘崇墵閿濈偤宕ㄧ€涙鍘梺鍓插亝缁诲啴宕抽崷顓犵＜闁归偊鍘鹃埊鏇犵磼缂佹绠為柟顔荤矙濡啫霉闊彃鐏查柟顔筋殔椤繈姊荤€靛憡鏅兼繝纰樷偓鍐茬骇闁告梹鐟ラ锝夊箻椤旂⒈娼婇梺鎶芥暜閸嬫捇鏌熸搴ｅ笡缂佺粯绋掑蹇涘礈瑜嶉崺宀勬⒑绾懎袚缂侇喖娴烽崚鎺楀籍閸喎鈧粯淇婇鐐存珳缂併劌顭峰娲濞戣鲸顎嗛梻鍌氬鐎氼剟鎮惧畡鎵殕闁逞屽墴閸┾偓妞ゆ帒鍠氬鎰箾閸欏鑰块柕鍡楀暣瀹曘劍娼忛崜褏鈼ゆ繝鐢靛Т閿曘倝鎮ч崱娑欏€块柛顭戝亖娴滄粓鏌熼崫鍕棞濞存粍鍎抽—鍐Χ閸℃鈹涚紓鍌氱С缁舵岸濡存笟鈧鎾閳╁啯鐝栭梻渚€鈧偛鑻晶鎵磼椤斿墽甯涚紒缁樼箓椤繈顢橀悩鎻掔闂傚倷绀佺紞濠偽涢崸妤佸€块柨鏇楀亾闁宠棄顦抽ˇ鏌ユ婢舵劖鐓熸俊顖氭惈閺嗚京绱掑Δ浣告诞闁哄苯绉堕幉鎾礋椤愩倓妗撴俊銈囧Х閸嬫盯鏁冮鍫濊摕婵炴垶鐟х弧鈧梺鍛婂姀閺呮繈藝椤撱垺鈷戦梺顐ゅ仜閼活垱鏅堕鐐寸厱闁瑰瓨绻勭粔铏光偓瑙勬穿缂嶄線宕洪埀顒併亜閹烘垵顏柣鎾崇箻閺屾盯濡烽敐鍛瀳婵犳鍠栭張顒勫Φ閸曨垱鏅濋柍褜鍓涚槐鐐寸節閸パ嗘憰闂佺偨鍎辩壕顓㈠汲閸℃稒鐓冪憸婊堝礈閻旈鏆﹀ù鍏兼綑缁犳盯鏌涜箛锝呬簻婵炲樊鍙冮獮鍐樄鐎规洖銈搁幃銏犵暋閺夎銈夋⒒閸屾瑨鍏岄柛瀣ㄥ姂瀹曟洟鏌嗗鍛焾闁荤姵浜介崝搴㈢▔瀹ュ鐓ユ繝闈涙－濡插綊鏌嶉柨瀣瑨闂囧鏌ㄥ┑鍡欏鐞氭岸姊洪棃娑欘棞闁哥喐鎸冲濠氭晲婢跺﹦顔婇梺缁樺姉閺佹悂寮抽妶鍛傛棃鎮╅棃娑楁勃闂佹悶鍔岄悘婵嬫偩閻戣棄绠氶梺顓ㄩ檮椤庡洭姊绘担瑙勫仩闁稿﹥鐗曠叅婵せ鍋撳┑锛勬暬瀹曠喖顢涘槌栧敽闂備胶鎳撻悺銊ф崲瀹ュ棛顩峰┑鍌氭啞閻撴洟鏌曟径娑㈡閻忓骏闄勯幈銊︾節閸涱噮浠╅梺鍛婄墬閻楃姴顕ｉ幘顔藉€锋繛鏉戭儐鐎氱喎鈹戦敍鍕杭闁稿﹥鍨垮畷婵嗙暆閳ь剟骞戦姀銈呯闁绘﹩鍋勬禍楣冩⒒閸喓鈼ら柛瀣ㄥ灪椤ㄣ儵鎮欓幖顓犲姺缂備浇椴哥敮鎺曠亽闂備礁鐏濋鍛搭敂閻戞绡€闁汇垽娼ф禒鈺呮煙濞茶绨界紒杈╁仱閸┾偓妞ゆ巻鍋撻柍瑙勫灴椤㈡瑩宕崟銊ヤ壕婵°倐鍋撻柍钘夘樀閹晫绮欓崸妤€鏁归梻浣告惈濞层劑宕伴幘璇插偍闂侇剙绉甸埛鎴犵磽娴ｅ厜妫ㄦい蹇撶墕閻ゎ噣鏌熺粙鍨劉闁告瑥绻掗埀顒€绠嶉崕鍗灻洪悩璇茬；闁圭偓鏋奸弸鏃堟煕椤垵鏋熼柣蹇撶墦濮婅櫣娑甸崪浣告疂缂備浇椴稿ú鐔肩嵁閹达箑顫呴柣姗嗗亝閺傗偓闂備胶纭跺褔寮插鍫濈＝闂傚牊渚楀〒濠氭煏閸繃顥滅紒妤佸浮閺屾稓鈧綆浜濋ˉ銏⑩偓?
        vkDeviceWaitIdle(device.device());

        if (swapChain == nullptr) {
            swapChain = std::make_unique<SwapChain>(device, extent);
        } else {
            std::shared_ptr<SwapChain> oldSwapChain = std::move(swapChain);

            swapChain = std::make_unique<SwapChain>(device, extent, oldSwapChain);

            if (!oldSwapChain->compareSwapFormats(*swapChain)) {
                throw std::runtime_error("Swap chain's image or depth format has changed");
            }

        }

        loadPickingResources();
    }

    bool Renderer::UpdateSceneViewportLayout(const ViewportRect &scenePanelRect, const ViewportRect &sceneViewportRect) {
        const VkExtent2D newSceneExtent{
            ClampExtentDimension(sceneViewportRect.width),
            ClampExtentDimension(sceneViewportRect.height)};
        const bool sceneExtentChanged = newSceneExtent.width != m_sceneRenderExtent.width ||
                                        newSceneExtent.height != m_sceneRenderExtent.height;
        const bool scenePanelChanged = !SameRect(m_scenePanelRect, scenePanelRect);
        const bool sceneViewportChanged = !SameRect(m_sceneViewportRect, sceneViewportRect);

        if (!sceneExtentChanged && !scenePanelChanged && !sceneViewportChanged) {
            return false;
        }

        m_scenePanelRect = scenePanelRect;
        m_sceneViewportRect = sceneViewportRect;

        if (!sceneExtentChanged) {
            return false;
        }

        vkDeviceWaitIdle(device.device());
        m_sceneRenderExtent = newSceneExtent;
        loadOffscreenResources();
        loadPickingResources();
        return true;
    }

    void Renderer::freeCommandBuffers() {
        if (commandBuffers.empty()) {
            return;
        }
        //濠电姷鏁告慨鐑藉极閸涘﹥鍙忛柣鎴ｆ閺嬩線鏌熼梻瀵割槮缁炬儳顭烽弻锝夊箛椤掍焦鍎撻梺鎼炲妼閸婂潡寮诲☉銏╂晝闁挎繂妫涢ˇ銉х磽娴ｅ搫校鐟滄澘鍟村﹢渚€姊洪幐搴ｇ畵闁瑰啿閰ｈ棢闊洦姊荤粻楣冩倵濞戞瑯鐒藉褏鏁婚弻锛勪沪閻愵剛顦ㄧ紓浣虹帛缁嬫牠藝閺屻儲鐓曢柣鏇氱娴滀即鏌熼缂存垹鎹㈠┑瀣倞闁靛鍨虹€氬ジ姊绘担鍛婂暈闁瑰憡妲掗妵鎰板礃閳哄喚娲搁梺缁樺姉閸庛倝鎮￠弴銏＄厽婵☆垱瀵ч悵顏嗏偓瑙勬礀閻倿寮诲☉銏犵厴闁割煈鍠氭导鍫ユ倵濞堝灝鏋熼柟姝屾珪閹便劑鍩€椤掑嫭鐓ユ繛鎴灻鈺傤殽閻愯尙澧涘ǎ鍥э躬閹瑩顢旈崟銊ヤ壕闁哄稁鍘肩粈澶嬩繆閵堝嫮鍔嶉柛娆忕箻閺岀喓绱掗姀鐘崇亪缂備讲鍋撳璺侯儑缁♀偓婵犵數濮撮崐缁樻櫠濞戙垺鐓熼柟鎯ь嚟濞叉挳鏌＄仦鍓р槈闁宠棄顦靛畷锟犳倷鐎甸晲鎲鹃梻鍌欒兌椤牓鏁冮妷鈺佺婵炲棙鎸搁拑鐔兼煃閵夈儳锛嶉柡鍡楁閹鏁愭惔鈥愁潾闂佷紮绠戦悧鎾诲箖濡ゅ啯鍠嗛柛鏇ㄥ墰椤︺劑鏌ｉ姀鈺佺仭閻㈩垳鍠栭幃姗€骞掑Δ浣哄幗闂佺粯鍔曢顓㈠煝閹剧粯鐓冪憸婊堝礈濞嗘挸鍌ㄩ柡宥冨妿閻濆爼鏌￠崶銉ョ仾闁抽攱鍨块弻娑樷攽閸℃浼€闂佸疇顕чˇ鐢稿蓟濞戞埃鍋撻敍鍗炲暕婢规洟姊婚崒娆掑厡妞ゎ厼鐗撻、鏍幢濞戞顔囬梺褰掓？缁躲倗绱為弽銊х瘈闂傚牊渚楅崕娑㈡煛娴ｅ憡顥滈棁澶愭煥濠靛棙澶勬繛鍛礃閵囧嫰濮€閻樺啿娈楀┑顔硷功缁垶骞忛崨顔剧懝妞ゆ牗绋掗弳鐐烘⒑鐠囪尙绠扮紒缁樺灴閹兘鏁冮崒姘辨煣闁荤姾妗ㄧ紞宥夊籍閸繄顦ㄥ銈呯箰濡盯寮抽妷鈺傗拻濞达綀娅ｇ敮娑欐叏婵犲偆鐓肩€规洏鍨奸ˇ褰掓煙椤旀儳浠辩€规洖缍婇、鏇㈡偐鏉堚晝娉块梻鍌欑濠€閬嶅磿閵堝鍨傞柣銏犲閺佸倿鏌嶉崫鍕櫤闁绘挻鐩幃妤呮晲閸屾稒鐝栫紓浣瑰姈濡啴寮婚垾宕囨殕闁逞屽墴瀹曚即寮介鐘茬ウ闂佺鎻粻鎴犵不濞戙垺鈷掗柛顐ゅ枔閳洘銇勯弬鍨仾缂佺粯绻勯崰濠偽熷ú缁樼秹闂備胶顭堥鍛村箠濮椻偓瀵偊宕橀纰辨綂闂侀潧鐗嗛幊鎾诲箺閺囥垺鈷戦柛婵嗗閸屻劑鏌涢妸銉хШ闁哄苯顑夊畷鍫曞Ω瑜忛惁鍫ユ⒒閸屾氨澧涚紒瀣笧缁﹪鍩￠崒娆戠畾濡炪倖鍔戦崐鏇熺濠婂嫨浜滄い蹇撳閺嗭絽鈹戦垾宕囧煟鐎规洏鍔戦、娑橆煥鎼粹剝鏆┑鐘垫暩婵參骞忛崘顔肩妞ゆ梻鍘ч崣濠囨⒒娴ｈ櫣銆婇柡鍌欑窔瀹曟垿骞橀幇浣瑰瘜闂侀潧鐗嗗Λ妤冪箔閸屾粎纾奸悹浣告贡缁♀偓閻庤娲﹂崹鐢电不濞戞ǚ妲堟繛鍡樺灥楠炲牓姊绘担铏瑰笡闁挎氨鐥紒銏犲箺闁哄懎鐖奸弻鍡楊吋閸℃瑥骞嶆俊銈囧Х閸嬫盯鎮樺┑瀣€堕柍鍝勫€舵禍婊勩亜閹板墎鎮奸柣顓熷浮閺屾盯鍩為幆褌澹曞┑锛勫亼閸婃牕顔忔繝姘；闁圭偓鐣禍婊堟煛閸ユ湹绨界紒澶樺枟閵囧嫰濮€閳╁啰顦伴梺杞扮閸熸挳宕洪埀顒併亜閹烘垵鈧粯绋夊鍛斀闁绘ê纾。鏌ユ煕婵犲嫭鏆慨濠傤煼瀹曞ジ鎮㈢悰鈥愁潓闂備胶鎳撳鍫曞箖閸岀偛钃熼柣鏂垮悑閹偞銇勯幇鈺佲偓妤呮偪閸曨偀鏀芥い鏃傘€嬮弨缁樹繆閻愯埖顥夐摶鐐烘煕閹扳晛濡锋俊鎻掔墦瀵爼宕煎☉妯侯棊闂侀潧鐗嗛ˇ浼村煕閹达附鐓曟繛鎴烇公閺€濠氭煟閹惧崬鍔ょ紒杈ㄥ笚瀵板嫮浠﹂挊澶婄濠电姷顣槐鏇㈠极婵犳氨宓侀柛銉墻閺佸秹鏌ㄥ┑鍡橆棏闁哄鐭傚濠氬磼濞嗘埈妲梺鍦拡閸嬪﹪骞嗘径瀣檮缂佸娉曢悰銉╂⒑鐟欏嫭绶查柛娆愭崌瀹曞爼顢楁担闀愮綍闂備礁澹婇崑鍡涘窗鎼淬劌绀夐柛娑樼摠閳锋垿鏌ｉ幇顖涱棄闁告梹宀搁弻娑㈡偄缂佹銆婄紓渚囧枛椤兘寮幇顓炵窞閻庯急鍕伜婵犵數鍋犻幓顏嗙礊閳ь剙鈹戦鍝勨偓婵囦繆閹绢喖绀冩い鏃傛櫕閸樹粙姊虹涵鍛仩闁稿鍠栭幃楣冨垂椤愵偅顔旈梺缁樺姈濞兼瑩鎮樼€电硶鍋撶憴鍕闁稿骸銈哥瘬濞撴埃鍋撻柡灞剧洴婵″爼宕卞Ο纰辨О闂備線娼уú銈団偓姘嵆閻涱噣骞掑Δ鈧粻锝嗙節閸偄濮冮柟顕嗙秮濮婃椽骞栭悙鎻掑闂佸湱顭堥崯鍧楋綖韫囨稒鎯為柣鐔告緲椤曆囨⒑閸濆嫭宸濋柛瀣洴閸╂稒寰勬繝搴㈠瘜闂侀潧鐗嗛崯顐ｄ繆閼测晝纾肩紓浣姑悘鈺呮煃瑜滈崜鐔奉焽瑜旀俊鍫曞箹娴ｅ搫绁﹂梺鍝勭▉閸樿偐绮婚鐣岀闁规崘鍩栭弳瀣⒒閸屾艾鈧悂宕愰幖浣哥９闁归棿绀佺壕褰掓煙闂傚顦︾痪鎯ф贡閳ь剛鎳撶€氼參宕崇壕瀣ㄤ汗闁圭儤鍨归崐鐐烘偡濠婂喚妯€鐎殿喗鎮傚浠嬵敇閻斿搫骞愰梻浣规偠閸庮垶宕曢柆宥嗗€堕柍鍝勫暟绾惧ジ鏌熼柇锕€寮炬繛鍫熺矒閺屸€崇暆閳ь剟宕伴弽顓炵畺鐟滄柨鐣锋總鍛婂亜闁告繂瀚▓銉╂⒒閸屾瑧顦﹂柟璇х節瀹曞湱鎲撮崟顒€寮块梺鍦檸閸犳牠鎮″鈧弻鐔告綇妤ｅ啯顎嶉梺绋款儐閸旀瑩骞冨Δ鍛嵍妞ゆ挾鍊姀掳浜滈柕澶涘缁犳绱掓潏銊﹀鞍闁瑰嘲鎳樺畷顐﹀礋绾版ê浜剧€光偓閸曨剛鍘遍梺闈涱焾閸庢娊宕洪敐鍥ｅ亾濞堝灝鏋涙い顓犲厴瀵偊骞囬鐐电獮婵犵數濮寸€氱兘宕ラ崨瀛樷拻濞达絿鎳撻婊呯磼鐠囨彃鈧潡鐛径濞炬闁靛繒濮烽鎺旂磽閸屾瑧鍔嶆い顓炴喘瀹曘垽骞栨担鍏夋嫼闂佸憡绋戦敃锝囨闁秵鐓曢柣妯诲墯濞堟粎鈧娲橀崝娆撶嵁閸ヮ剚鍋嬮柛顐犲灩楠炴劙鏌ｆ惔鈥冲辅闁稿鎹囬幃妤呮晲鎼粹€愁潾闂佷紮绠戦悧鎾诲箖濡ゅ啯鍠嗛柛鏇ㄥ墰椤︺劑鏌ｉ姀鈺佺仭閻㈩垳鍠栭幃姗€骞掑Δ浣哄幗闂佺粯鍔曢顓㈠煝閹剧粯鐓冪憸婊堝礈濞嗘挸鍌ㄩ柡宥冨妿閻濆爼鏌￠崶銉ョ仾闁抽攱鍨块弻娑樷攽閸℃浼€闂佸疇顕чˇ鐢稿蓟濞戞埃鍋撻敍鍗炲暕婢规洟姊婚崒娆掑厡妞ゎ厼鐗撻、鏍幢濞戞顔囬梺褰掓？缁躲倗绱為弽銊х瘈闂傚牊渚楅崕娑㈡煛娴ｅ憡顥㈤柡灞剧〒娴狅箓宕滆閺呭ジ姊洪棃娴ュ牓寮插鍫濈厱闁硅揪闄勯悡鏇熺節闂堟稒顥滄い蹇ｅ墯閵囧嫰顢曢鍌滄殼闂佸搫鏈惄顖氼嚕椤曗偓閸┾偓妞ゆ帒瀚崹鍌炴煕瑜庨〃鍫濈暤娓氣偓閺屾盯骞囬棃娑欑亪濡炪値鍋勯幊姗€寮婚弴锛勭杸濠电偞鍎虫禍鍓р偓瑙勬礀濞层倝鏌婇柆宥嗏拻闁稿本鐟чˇ锕傛煙鐠囇呯瘈闁诡喚鍏樻俊鐑藉煛娴ｇ尨绱梻渚€娼荤€靛矂宕㈤挊澶屾殾闁哄被鍎查悡鏇熴亜閹邦喖孝闁告梹姘ㄧ槐鎺楁偐瀹曞洤鈪归梻鍥ь樀閺岋絽螣閻戞ǚ鏋欓梺绋垮閻╊垶寮婚埄鍐╁闁告縿鍎涜閺岋紕浠︾拠鎻掝瀳闂佸疇顫夐崹鍨暦閸楃倣鐔兼倻濮楀棔鐢婚梻鍌氬€峰ù鍥敋瑜忛幑銏ゅ箛椤斿墽鐓嬮悷婊呭鐢帡鎷戦悢鍏肩叆婵犻潧妫欓ˉ鐐烘煕鎼达絽鏋涢柡灞炬礉缁犳盯濡疯閸╁苯顪冮妶鍐ㄥ姕缂佽瀚Σ鎰板箻鐠囪尙锛滃┑顔缴戦惁鐑藉閵堝棛鍙嗛梺鍝勬处閿氶柍褜鍓氱换鍫ョ嵁閸愵喖绠氱憸蹇涘汲鐎ｎ喗鐓欓梺鍨儐閳锋劖淇婇銏犳殭闁伙絿鍏樻俊鎼佸煛婵犲啯娅嶉梻浣规偠閸庤崵寰婇幆褉鏋嶅┑鐘叉处閻撴盯鎮橀悙鍨珪閺佸牓鎮楃憴鍕闁稿锕ら悾鐑芥偄绾拌鲸鏅濋梺缁樻濞撹绔熼弴銏♀拺闁煎鍊曢弸鎴濐熆閻熼偊鍎旂€殿喚顭堥鍏煎緞鐎ｎ剙甯鹃梻濠庡亜濞层倝顢栭崨鏉戠劦妞ゆ帊鑳舵晶鐢告煕閳哄倻娲存鐐差儔閹瑩寮堕幋婵呯礋闂傚倷娴囧畷鐢稿窗閹惧瓨娅犳俊銈呮噹缁犲綊鏌熼柇锕€鍘撮柡鈧禒瀣厓闁靛鍎遍弳閬嶆煙椤旇棄鐏ラ懣鎰版煕閵夋垵绉烽崥顐㈩渻閵堝啫鐏繛鑼枛瀵偊骞囬鐔峰妳闂侀潧绻堥崹鍝勑掓径瀣瘈闁汇垽娼ф禒婊堟煟鎺抽崝搴ㄥ礆閹烘埈鍚嬪璺猴功閻ｆ椽姊虹粙鎸庢拱婵ǜ鍔戝鎶芥晝閸屾稓鍘繝鐢靛仧閸嬫挸鈻嶉崨顖滅＜闁绘瑦鐟ュú锕傛偂濞嗘挻鍊甸柣銏☆問閻掔偓銇勬惔锛勭劯闁哄本鐩幃娆撳垂椤愶絾鐦撴繝娈垮枛閿曪妇鍒掗鐐茬闁告稑鐡ㄩ幆鐐烘煟閻旂顥嬫い鎰偢濮婄粯鎷呯憴鍕哗闂佸憡鏌ㄩ惌鍌氱暦閵壯€鍋撻敐搴℃灈缂佺姷鍋ら弻鏇熺箾閻愵剚鐝旈柛銉ョ摠缁绘繈濮€閿濆棛銆愰梺鎸庢穿缁犳挸顕ｉ弰蹇ｆ▌濠殿喖锕紓姘跺Φ閹版澘绠抽柟鍨閸氬綊姊绘担铏瑰笡閻㈩垼浜炵划濠氬箻瀹曞洦娈鹃梺纭呮彧缁犳垿鎮炲ú顏呯厱闁规澘鍚€缁ㄨ姤銇勯弮鈧ú鐔奉潖閾忕懓瀵查柡鍥╁仜閳峰姊洪幐搴ゅ闁宠鍨块、娆撳礂閻撳孩鐏庢繝娈垮枛閿曘儱顪冮挊澶屾殾闁挎繂妫楃欢鐐碘偓鍏夊亾闁逞屽墴閹繝宕掑鍛紳婵炶揪绲肩划娆撳传濞差亝鐓欓柛娑橈攻閸婃劖銇勯姀鈭╂垹缂撻悾宀€鐭欓柛顭戝枤閻涒晜淇婇悙顏勨偓鏍蓟閵娾晛瑙﹂悗锝庡枟閸婂爼骞栭幖顓熺窔缂佽妫濋弻鏇㈠醇濠靛洦鎮欓柛鐔告倐濮婅櫣绮欏▎鎯у壉缂備礁顑嗛幑鍥嵁閹达箑顫呴柣娆屽亾婵炵鍔戦弻宥堫檨闁告挾鍠栭幃浼搭敋閳ь剙鐣峰鈧垾锕傚箣閻樻彃姹查梻鍌欑婢瑰﹪宕戦崱娑樼獥閹肩补妾ч弸鏃堟煟閺傚灝鎮戦柍閿嬪灴濮婂宕奸悢鍓佺箒濠碉紕瀚忛崶銊㈡嫽闂佸憡娲﹂崑鍕敂椤撶喆浜滈柕蹇婂墲椤ュ牓鏌熼鍝勭伈鐎规洘顨堟禍鎼佸冀閵娿儺浼滃┑鐘垫暩閸嬬娀骞撻鍡楃筏濞寸姴顑呯粻鐔兼煥閻斿搫孝缂佺姵宀搁弻娑㈩敃閻樻彃濮曢梺缁樻尰濞叉牠鍩為幋锔藉亹闁圭粯宸婚崑鎾寸節濮橆剚妲梺鎸庣箓濞茬娀宕戦幘鑸靛枂闁告洦鍓涢ˇ銊╂⒑閸涘﹥鈷愰柣鐔村劦椤㈡岸鏁愰崱娆樻祫闁诲函绲介悘姘跺疾濠靛鈷戠紓浣股戦ˉ鍡樼箾閹捐娼愮紒鏃傚枛瀵挳鎮欓埡鍌涙澑婵＄偑鍊栧褰掑几缂佹鐟规繛鎴欏灪閻撴洟鏌熼幆褜鍤熼柕鍡樺笧缁辨帡顢欓懞銉у弳闁绘挶鍊栭妵鍕疀閹炬潙娅ч梺鍛娚戦幃鍌炲蓟閿濆牏鐤€闁哄洨鍋橀崫妤€顪冮妶鍡樿偁闁告洦鍓欏鍧楁⒑缁嬭法鐏遍柛瀣仱閹€斥枎閹扳晙绨婚梺鍝勫€圭€笛囧箚閸儲鐓熼煫鍥ㄨ壘閻ㄦ椽鏌嶈閸撴繈锝炴径濞掗缚绠涘☉妯碱槷閻庡箍鍎卞ú锕€鐣烽崣澶岀瘈闂傚牊渚楅崕蹇曠磼閳ь剛鈧綆鍋佹禍婊堢叓閸ャ劍灏版い銉у█閺屻劌鈽夊顒佺亪濠殿喖锕ら…宄扮暦閹烘埈娼╂い鎴ｆ娴滄儳顪冪€ｎ亝鎹ｉ柣顓炴闇夐柨婵嗙墛椤忕姷绱掗埀顒佺節閸屾鏂€闂佺粯锚瀵墎娑甸挊澶樼唵闁兼悂娼ф慨鍥╃磼閻樺樊鐓奸柟顔肩秺瀹曞爼顢旈崟顓燁嚄婵＄偑浼呮担绋挎殜闁衡偓閼恒儯浜滈柡鍌涱儥濞肩喎霉閻樺磭鐭掗柡灞剧〒閳ь剨缍嗛崑鍛焊閻㈠憡鐓冪憸婊堝礈濮橀鏁婇柡宥庡幖闂傤垱銇勯弴妤€浜鹃悗瑙勬礃閸ㄥ墎鎹㈠┑瀣倞闁宠桨鑳堕妶锔界節绾版ɑ顫婇柛銊ゅ嵆瀹曘儳鈧綆鍏橀崑鎾愁潩椤撶偛鎽甸梺鍝勬湰缁嬫垿鍩㈡惔銈囩杸闁挎繂妫岄弸鏃€绻濈喊妯活潑闁稿妫濆畷浼村冀瑜滃鏍磽娴ｈ鐒介柣鐔风秺閺屽秷顧侀柛鎾寸懇閳ワ箓宕堕宥嗏枌闂備浇顕栭崯顐﹀炊瑜嶉懓鍨攽閿涘嫬浜奸柛銊ㄦ缁瑩骞嬮敐鍥︾胺闂傚倷绀侀幉锛勫垝閸儲鍊块柨鏇炲€搁悿鐐節婵犲倻澧涢柍閿嬪灴閺屾稑鈽夊鍫熸暰闁诲繐娴氶崑濠囧箖鐟欏嫭濯撮柛锔诲幖瀵劑鎮楃憴鍕闁搞劌娼￠悰顔碱潨閳ь剙顕ｉ鍕ㄩ柨鏂垮綖缁ㄦ挳姊婚崒娆戭槮濠㈢懓锕幃锟犲醇閵夈儳鐛ュ┑掳鍊曢幊蹇涘磹閸洘鐓曟い鎰Т閻忣喚鐥幑鎰棄闂囧鏌ㄥ┑鍡欏妞ゅ繒濞€閹粙顢涘☉姘垱闂佸搫琚崝鎴濐嚕閺夋嚦鐔兼惞闁稒鍋呭┑锛勫亼閸婃牕煤閳哄啰绀婂〒姘ｅ亾闁靛棔绶氬鎾閳╁啯鐝栭梻渚€鈧偛鑻晶鎵磼椤斿墽甯涚紒缁樼箓椤繈顢橀悙鏉垮闂傚倷鐒﹂幃鍫曞磿濠婂牆绀冩い蹇撶墢瀹曟粌鈹戦敍鍕杭闁稿﹥鍨垮畷鏇㈡嚑椤掍礁搴婇梺鍓插亝缁诲秴顭囬弽銊х鐎瑰壊鍠曠花鍏笺亜閵夈儳澧涚紒缁樼洴楠炲鎮欑捄渚婵＄偑鍊х徊浠嬪箹椤愶箑鐓橀柟杈剧畱閻擄繝鏌涢埄鍐︿沪缂併劎鍘ч—鍐Χ閸愩劌顬堥梺鎸庢处娴滎亝淇婄€涙鐟归柍褜鍓欓锝夘敋閳ь剙鐣烽悡搴樻斀閻庯綆浜濋弳顏堟⒒閸屾瑧顦﹂柟纰卞亜鐓ら柕濞炬櫅閻ゎ噣鏌涜椤ㄥ懐绮婚娑氱鐎瑰壊鍠曠花濂告煟閹捐泛鏋涢柡宀嬬到铻ｉ柛婵嗗缁楊參姊洪悡搴☆棌濞存粠浜璇测槈閵忕姵顥濋柣鐘充航閸斿酣宕濋鐐村€垫繛鍫濈仢閺嬬喖鏌熼鐓庘偓鍧楁偘椤旈敮鍋撻敐搴℃灍闁哄懏绮撻弻锝夋晲閸涙澘顏Δ鐘靛仦閸旀瑥顫忛搹瑙勫珰闁圭粯甯掑В鎰磽閸屾氨孝婵☆偅鐟х划瀣吋婢跺﹪鍞堕梺鍝勬川閸犲孩绂嶅Δ鍛拺缂佸娉曠粻鎶芥煃瀹勬壆澧曢柍缁樻尰缁傛帞鈧綆鍋嗛崢浠嬫⒑瑜版帒浜伴柛銊ゅ嵆閹啴鎼归崷顓狅紲闂佺粯锚濡﹪鎮℃總鍛婄厸鐎光偓閳ь剟宕伴弽顓溾偓浣糕枎閹炬潙浠奸悗鍏夊亾闁逞屽墴閹剝寰勯幇顓涙嫼闂佽崵鍠愭竟鍡涙晬瀹ュ鐓曢悗锝冨妼閳ь剚鐗楃粚杈ㄧ節閸ャ劌鈧鏌ら幁鎺戝姕婵炲懎妫涚槐鎾存媴閸︻厸妲堝銈嗗灥鐎氫即鐛€ｎ喗鍊婚柤鎭掑劗閹峰姊虹粙鎸庢拱闁煎綊绠栭崺鈧い鎺戝濡垹绱掗鑲╁缂佹鍠栭崺鈧い鎺戝瀹撲線鏌″搴″季闁轰礁鍟撮弻銊╁即濡も偓娴滃墽绱掗悙顒€鍔ょ紓宥咃躬瀵鈽夐姀鈺傛櫇闂佹寧绻傚Λ娑⑺囬妷褏纾奸柣鎰靛墮閸斻倗绱撳鍜冭含鐎殿喖顭烽崹楣冨箛娴ｅ憡鍊梺纭呭亹鐞涖儵鍩€椤掑啫鐨洪柣鏍憾濮婄粯鎷呮笟顖滃姼濡炪倖鍨靛Λ婵嬬嵁閹版澘绠瑰〒姘功缁嬪繐鈹戦悩缁樻锭妞ゆ垶鍔欏顐﹀幢濞戞瑧鍘遍柣蹇曞仜婢т粙寮弽顓熺厪濠电偟鍋撳▍鍡涙煃闁垮鐏撮柡灞剧☉閳规垿宕卞Δ濠佺磽婵＄偑鍊ら崑鎾剁不閹捐钃熸繛鎴欏灩缁秹鏌嶈閸撶喎鐣疯ぐ鎺戦敜婵°倕鍟粊锕傛⒑閸涘﹤濮﹂柛鐘崇墵閿濈偤宕ㄧ€涙鍘梺鍓插亝缁诲啴宕抽崷顓犵＜闁归偊鍘鹃埊鏇犵磼缂佹绠為柟顔荤矙濡啫霉闊彃鐏查柟顔筋殔椤繈姊荤€靛憡鏅兼繝纰樷偓鍐茬骇闁告梹鐟ラ锝夊箻椤旂⒈娼婇梺鎶芥暜閸嬫捇鏌?
        vkFreeCommandBuffers(device.device(), device.getCommandPool(), static_cast<uint32_t>(commandBuffers.size()),
                             commandBuffers.data());
        commandBuffers.clear();
    }

    void Renderer::beginSwapChainRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call beginShadowRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot begin renderShadow pass on command buffer from a different frame");

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = swapChain->getRenderPass();
        renderPassBeginInfo.framebuffer = swapChain->getFrameBuffer(currentImageIndex);

        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = swapChain->getSwapChainExtent();

        VkClearValue clearValues[2];
        clearValues[0].color = {0.01f, 0.01f, 0.01f, 1.0f};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(2);
        renderPassBeginInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        const VkViewport viewport = ToViewport(m_sceneViewportRect);
        const VkRect2D scissor = ToScissor(m_sceneViewportRect);

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void Renderer::beginSceneColorRenderPass(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
        assert(isFrameStarted && "Cannot call beginSceneColorRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot begin scene color pass on command buffer from a different frame");
        assert(m_sceneColorRenderPass != VK_NULL_HANDLE && "Scene color render pass is not initialized");
        assert(imageIndex < m_sceneColorFramebuffers.size() && "Scene color framebuffer index is out of range");

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = m_sceneColorRenderPass;
        renderPassBeginInfo.framebuffer = m_sceneColorFramebuffers[imageIndex];
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = m_sceneRenderExtent;

        VkClearValue clearValues[2]{};
        clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_sceneRenderExtent.width);
        viewport.height = static_cast<float>(m_sceneRenderExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = m_sceneRenderExtent;

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }
    void Renderer::beginGizmosRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call beginGizmosRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() && "Cannot begin render gizmos pass on command buffer from a different frame");

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = swapChain->getGizmosRenderPass();
        renderPassBeginInfo.framebuffer = swapChain->getFrameBuffer(currentImageIndex);

        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = swapChain->getSwapChainExtent();

        VkClearValue clearValues[2];
        clearValues[0].color = {0.01f, 0.01f, 0.01f, 1.0f};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(2);
        renderPassBeginInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        const VkViewport viewport = ToViewport(m_sceneViewportRect);
        const VkRect2D scissor = ToScissor(m_sceneViewportRect);

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void Renderer::beginPickingRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call beginPickingRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot begin render picking pass on command buffer from a different frame");

        if (m_pickingRenderPass == VK_NULL_HANDLE || m_pickingFramebuffer == VK_NULL_HANDLE ||
            m_pickingExtent.width == 0 || m_pickingExtent.height == 0) {
            return;
        }

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = m_pickingRenderPass;
        renderPassBeginInfo.framebuffer = m_pickingFramebuffer;
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = m_pickingExtent;

        VkClearValue clearValues[2]{};
        clearValues[0].color.int32[0] = -1;
        clearValues[0].color.int32[1] = 0;
        clearValues[0].color.int32[2] = 0;
        clearValues[0].color.int32[3] = 0;
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_pickingExtent.width);
        viewport.height = static_cast<float>(m_pickingExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = m_pickingExtent;

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }
    void Renderer::beginShadowRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call beginShadowRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot begin renderShadow pass on command buffer from a different frame");

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = shadowRenderPass;
        renderPassBeginInfo.framebuffer = shadowFrameBuffer;

        renderPassBeginInfo.renderArea.offset = {0, 0};
//        renderPassBeginInfo.renderArea.extent = swapChain->getSwapChainExtent();
        renderPassBeginInfo.renderArea.extent.width = ShadowMapResolution;
        renderPassBeginInfo.renderArea.extent.height = ShadowMapResolution;

        VkClearValue clearValue{};
        clearValue.depthStencil.depth = 1;
        clearValue.depthStencil.stencil = 0;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
//        viewport.m_windowWidth = static_cast<float>(m_window.getCurrentExtent().m_windowWidth);
//        viewport.m_windowHeight = static_cast<float>(m_window.getCurrentExtent().m_windowHeight);
        viewport.width = static_cast<float>(ShadowMapResolution);
        viewport.height = static_cast<float>(ShadowMapResolution);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
//        scissor.extent = swapChain->getSwapChainExtent();
        scissor.extent.width = ShadowMapResolution;
        scissor.extent.height = ShadowMapResolution;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void Renderer::endPickingRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call endPickingRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot end picking render pass on command buffer from a different frame");

        if (m_pickingRenderPass == VK_NULL_HANDLE || m_pickingFramebuffer == VK_NULL_HANDLE ||
            m_pickingExtent.width == 0 || m_pickingExtent.height == 0) {
            return;
        }

        vkCmdEndRenderPass(commandBuffer);
        m_hasPickingData = true;
    }
    void Renderer::endShadowRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call endShadowRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot end renderShadow pass on command buffer from a different frame");

        vkCmdEndRenderPass(commandBuffer);
    }

    void Renderer::endSwapChainRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call endSwapChainRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() && "Cannot endSwapChainRenderPass on command buffer from a different frame");

        vkCmdEndRenderPass(commandBuffer);
    }

    void Renderer::endSceneColorRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call endSceneColorRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot end scene color pass on command buffer from a different frame");

        vkCmdEndRenderPass(commandBuffer);
    }
    void Renderer::endGizmosRenderPass(VkCommandBuffer commandBuffer) {
        assert(isFrameStarted && "Cannot call endGizmosRenderPass while frame is not in progress");
        assert(commandBuffer == getCurrentCommandBuffer() &&
               "Cannot endGizmosRenderPass on command buffer from a different frame");

        vkCmdEndRenderPass(commandBuffer);
    }

    void Renderer::createCommandBuffers() {

        VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
        commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.pNext = nullptr;
        commandBufferAllocateInfo.commandPool = device.getCommandPool();
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount = SwapChain::MAX_FRAMES_IN_FLIGHT;

        commandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateCommandBuffers(device.device(), &commandBufferAllocateInfo, commandBuffers.data()) !=
            VK_SUCCESS) {
            throw std::runtime_error("Cannot allocate command buffers");
        }
    }

    void Renderer::freeShadowResources() {
        if (shadowRenderPass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device.device(), shadowRenderPass, nullptr);
        if (shadowFrameBuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device.device(), shadowFrameBuffer, nullptr);
    }

    void Renderer::loadShadow() {
        freeShadowResources();

        shadowImage = std::make_shared<Image>(device);
        VkImageCreateInfo imageCreateInfo{};
        Image::setDefaultImageCreateInfo(imageCreateInfo);
        VkExtent3D shadowMapExtent{};
//        shadowMapExtent.m_windowHeight = swapChain->getSwapChainExtent().m_windowHeight;
//        shadowMapExtent.m_windowWidth = swapChain->getSwapChainExtent().m_windowWidth;
        shadowMapExtent.height = ShadowMapResolution;
        shadowMapExtent.width = ShadowMapResolution;

        shadowMapExtent.depth = 1;
        if (isCubeMap) {
            imageCreateInfo.arrayLayers = 6;
            imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        }
        imageCreateInfo.format = VK_FORMAT_D32_SFLOAT;
        imageCreateInfo.extent = shadowMapExtent;
        imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        shadowImage->createImage(imageCreateInfo);

        //create shadow image view
        auto imageViewCreateInfo = std::make_shared<VkImageViewCreateInfo>();
        shadowImage->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
        if (isCubeMap) {
            imageViewCreateInfo->subresourceRange.layerCount = 6;
            imageViewCreateInfo->viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        }
        imageViewCreateInfo->format = VK_FORMAT_D32_SFLOAT;
        imageViewCreateInfo->components.r = VK_COMPONENT_SWIZZLE_R;
        imageViewCreateInfo->components.g = VK_COMPONENT_SWIZZLE_G;
        imageViewCreateInfo->components.b = VK_COMPONENT_SWIZZLE_B;
        imageViewCreateInfo->components.a = VK_COMPONENT_SWIZZLE_A;
        imageViewCreateInfo->subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        shadowImage->createImageView(*imageViewCreateInfo);

        //create shadow sampler
        shadowSampler = std::make_shared<Sampler>(device);
        shadowSampler->createTextureSampler();

        //create shadow pass
        VkAttachmentDescription attachmentDescriptions[2];
        attachmentDescriptions[0].format = VK_FORMAT_D32_SFLOAT;
        attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescriptions[0].flags = 0;
        attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 0;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription[1];
        subpassDescription[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription[0].flags = 0;
        subpassDescription[0].inputAttachmentCount = 0;
        subpassDescription[0].pInputAttachments = nullptr;
        subpassDescription[0].colorAttachmentCount = 0;
        subpassDescription[0].pColorAttachments = nullptr;
        subpassDescription[0].pResolveAttachments = nullptr;
        subpassDescription[0].pDepthStencilAttachment = &depthAttachmentRef;
        subpassDescription[0].preserveAttachmentCount = 0;
        subpassDescription[0].pPreserveAttachments = nullptr;

        VkRenderPassCreateInfo renderPassCreateInfo{};

        VkRenderPassMultiviewCreateInfo multiviewCreateInfo{};
        multiviewCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
        multiviewCreateInfo.subpassCount = 1;
        //All 6 faces of the cube map
        uint32_t viewMask = 0b00111111;
        multiviewCreateInfo.pViewMasks = &viewMask;
        multiviewCreateInfo.correlationMaskCount = 0;
        multiviewCreateInfo.pCorrelationMasks = nullptr;

        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.pNext = &multiviewCreateInfo;
        renderPassCreateInfo.attachmentCount = 1;
        renderPassCreateInfo.pAttachments = attachmentDescriptions;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = subpassDescription;
        renderPassCreateInfo.dependencyCount = 0;
        renderPassCreateInfo.pDependencies = nullptr;
        renderPassCreateInfo.flags = 0;

        if (vkCreateRenderPass(device.device(), &renderPassCreateInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create SHADOW renderShadow pass");
        }

        //create frame buffer
        VkFramebufferCreateInfo framebufferCreateInfo{};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.pNext = nullptr;
        framebufferCreateInfo.renderPass = shadowRenderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = shadowImage->getImageView();
//        framebufferCreateInfo.m_windowWidth = swapChain->getSwapChainExtent().m_windowWidth;
//        framebufferCreateInfo.m_windowHeight = swapChain->getSwapChainExtent().m_windowHeight;

        framebufferCreateInfo.width = ShadowMapResolution;
        framebufferCreateInfo.height = ShadowMapResolution;

        framebufferCreateInfo.layers = 1;
        framebufferCreateInfo.flags = 0;

        if (vkCreateFramebuffer(device.device(), &framebufferCreateInfo, nullptr, &shadowFrameBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create SHADOW frame buffer");
        }
    }

    void Renderer::freePickingResources() {
        if (m_pickingFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device.device(), m_pickingFramebuffer, nullptr);
            m_pickingFramebuffer = VK_NULL_HANDLE;
        }

        m_pickingIdImage.reset();
        m_pickingDepthImage.reset();
        m_pickingReadbackBuffer.reset();
        m_pickingExtent = {0, 0};
        m_hasPickingData = false;
    }

    void Renderer::loadPickingResources() {
        freePickingResources();

        const auto sceneExtent = m_sceneRenderExtent;
        if (sceneExtent.width == 0 || sceneExtent.height == 0) {
            return;
        }

        m_pickingExtent = sceneExtent;
        pickingDepthFormat = device.findSupportedFormat(
                {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT},
                VK_IMAGE_TILING_OPTIMAL,
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

        VkImageCreateInfo imageCreateInfo{};
        Image::setDefaultImageCreateInfo(imageCreateInfo);
        imageCreateInfo.extent = {m_pickingExtent.width, m_pickingExtent.height, 1};

        m_pickingIdImage = std::make_shared<Image>(device);
        imageCreateInfo.format = pickingIdFormat;
        imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        m_pickingIdImage->createImage(imageCreateInfo);

        VkImageViewCreateInfo imageViewCreateInfo{};
        m_pickingIdImage->setDefaultImageViewCreateInfo(imageViewCreateInfo);
        imageViewCreateInfo.format = pickingIdFormat;
        imageViewCreateInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        m_pickingIdImage->createImageView(imageViewCreateInfo);

        m_pickingDepthImage = std::make_shared<Image>(device);
        imageCreateInfo.format = pickingDepthFormat;
        imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        m_pickingDepthImage->createImage(imageCreateInfo);

        m_pickingDepthImage->setDefaultImageViewCreateInfo(imageViewCreateInfo);
        imageViewCreateInfo.format = pickingDepthFormat;
        imageViewCreateInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        m_pickingDepthImage->createImageView(imageViewCreateInfo);

        VkAttachmentDescription attachmentDescriptions[2]{};
        attachmentDescriptions[0].format = pickingIdFormat;
        attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachmentDescriptions[1].format = pickingDepthFormat;
        attachmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments = &colorAttachmentRef;
        subpassDescription.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        if (m_pickingRenderPass == VK_NULL_HANDLE) {
            VkRenderPassCreateInfo renderPassCreateInfo{};
            renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassCreateInfo.attachmentCount = 2;
            renderPassCreateInfo.pAttachments = attachmentDescriptions;
            renderPassCreateInfo.subpassCount = 1;
            renderPassCreateInfo.pSubpasses = &subpassDescription;
            renderPassCreateInfo.dependencyCount = 2;
            renderPassCreateInfo.pDependencies = dependencies;

            if (vkCreateRenderPass(device.device(), &renderPassCreateInfo, nullptr, &m_pickingRenderPass) != VK_SUCCESS) {
                throw std::runtime_error("failed to create editor picking render pass");
            }
        }

        VkImageView attachments[2] = {*m_pickingIdImage->getImageView(), *m_pickingDepthImage->getImageView()};
        VkFramebufferCreateInfo framebufferCreateInfo{};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = m_pickingRenderPass;
        framebufferCreateInfo.attachmentCount = 2;
        framebufferCreateInfo.pAttachments = attachments;
        framebufferCreateInfo.width = m_pickingExtent.width;
        framebufferCreateInfo.height = m_pickingExtent.height;
        framebufferCreateInfo.layers = 1;

        if (vkCreateFramebuffer(device.device(), &framebufferCreateInfo, nullptr, &m_pickingFramebuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create editor picking framebuffer");
        }

        m_pickingReadbackBuffer = std::make_shared<Buffer>(
                device,
                sizeof(int32_t),
                1,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_pickingReadbackBuffer->map();
    }

    int32_t Renderer::readPickingObjectId(uint32_t pixelX, uint32_t pixelY) {
        if (!m_hasPickingData || m_pickingIdImage == nullptr || m_pickingReadbackBuffer == nullptr ||
            m_pickingExtent.width == 0 || m_pickingExtent.height == 0) {
            return -1;
        }

        if (pixelX >= m_pickingExtent.width || pixelY >= m_pickingExtent.height) {
            return -1;
        }

        VkCommandBuffer commandBuffer = device.beginSingleTimeCommands();

        VkImageMemoryBarrier toTransferBarrier{};
        toTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransferBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferBarrier.image = m_pickingIdImage->getImage();
        toTransferBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &toTransferBarrier);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageOffset = {static_cast<int32_t>(pixelX), static_cast<int32_t>(pixelY), 0};
        copyRegion.imageExtent = {1, 1, 1};

        vkCmdCopyImageToBuffer(
                commandBuffer,
                m_pickingIdImage->getImage(),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                m_pickingReadbackBuffer->getBuffer(),
                1,
                &copyRegion);

        VkImageMemoryBarrier toColorAttachmentBarrier{};
        toColorAttachmentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toColorAttachmentBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toColorAttachmentBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toColorAttachmentBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toColorAttachmentBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toColorAttachmentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachmentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachmentBarrier.image = m_pickingIdImage->getImage();
        toColorAttachmentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &toColorAttachmentBarrier);

        device.endSingleTimeCommands(commandBuffer, "read_picking_object_id");

        const auto *mapped = static_cast<int32_t *>(m_pickingReadbackBuffer->getMappedMemory());
        return mapped == nullptr ? -1 : mapped[0];
    }
    void Renderer::freeOffscreenResources() {
        for (auto framebuffer: m_sceneColorFramebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device.device(), framebuffer, nullptr);
            }
        }
        m_sceneColorFramebuffers.clear();

        m_offscreenImageColors.clear();
        m_sceneColorImageColors.clear();
        m_shadowTermImageColors.clear();
        m_shadowMomentsImageColors.clear();
        m_worldPosImage.clear();
        m_rayTracingGuideImage.clear();
        m_denoisingAccumulationImage.reset();
        m_offscreenSampler.reset();
        offscreenImageDepth.reset();
    }

    void Renderer::loadOffscreenResources() {
        freeOffscreenResources();
        {
            m_offscreenSampler = std::make_shared<Sampler>(device);
            m_offscreenSampler->createTextureSampler();
            //Color

            VkImageCreateInfo imageCreateInfo{};
            Image::setDefaultImageCreateInfo(imageCreateInfo);
            imageCreateInfo.format = offscreenColorFormat;
            imageCreateInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
            VkExtent3D imageExtent{};
            imageExtent.height = m_sceneRenderExtent.height;
            imageExtent.width = m_sceneRenderExtent.width;
            imageExtent.depth = 1;
            imageCreateInfo.extent = imageExtent;

            m_offscreenImageColors.push_back(std::make_shared<Image>(device));
            m_offscreenImageColors.push_back(std::make_shared<Image>(device));
            m_offscreenImageColors[0]->createImage(imageCreateInfo);
            m_offscreenImageColors[1]->createImage(imageCreateInfo);
            auto imageViewCreateInfo = std::make_shared<VkImageViewCreateInfo>();
            m_offscreenImageColors[0]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            m_offscreenImageColors[0]->createImageView(*imageViewCreateInfo);
            m_offscreenImageColors[1]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            m_offscreenImageColors[1]->createImageView(*imageViewCreateInfo);
            m_offscreenImageColors[0]->sampler = m_offscreenSampler->getSampler();
            m_offscreenImageColors[1]->sampler = m_offscreenSampler->getSampler();

            VkImageCreateInfo sceneColorCreateInfo = imageCreateInfo;
            sceneColorCreateInfo.format = offscreenColorFormat;
            sceneColorCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            m_sceneColorImageColors.push_back(std::make_shared<Image>(device));
            m_sceneColorImageColors.push_back(std::make_shared<Image>(device));
            m_sceneColorImageColors[0]->createImage(sceneColorCreateInfo);
            m_sceneColorImageColors[1]->createImage(sceneColorCreateInfo);
            imageViewCreateInfo = std::make_shared<VkImageViewCreateInfo>();
            m_sceneColorImageColors[0]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = sceneColorCreateInfo.format;
            m_sceneColorImageColors[0]->createImageView(*imageViewCreateInfo);
            m_sceneColorImageColors[1]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = sceneColorCreateInfo.format;
            m_sceneColorImageColors[1]->createImageView(*imageViewCreateInfo);
            m_sceneColorImageColors[0]->sampler = m_offscreenSampler->getSampler();
            m_sceneColorImageColors[1]->sampler = m_offscreenSampler->getSampler();
            m_shadowTermImageColors.push_back(std::make_shared<Image>(device));
            m_shadowTermImageColors.push_back(std::make_shared<Image>(device));
            m_shadowTermImageColors[0]->createImage(imageCreateInfo);
            m_shadowTermImageColors[1]->createImage(imageCreateInfo);
            imageViewCreateInfo = std::make_shared<VkImageViewCreateInfo>();
            m_shadowTermImageColors[0]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            m_shadowTermImageColors[0]->createImageView(*imageViewCreateInfo);
            m_shadowTermImageColors[1]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            m_shadowTermImageColors[1]->createImageView(*imageViewCreateInfo);
            m_shadowTermImageColors[0]->sampler = m_offscreenSampler->getSampler();
            m_shadowTermImageColors[1]->sampler = m_offscreenSampler->getSampler();

            m_shadowMomentsImageColors.push_back(std::make_shared<Image>(device));
            m_shadowMomentsImageColors.push_back(std::make_shared<Image>(device));
            m_shadowMomentsImageColors[0]->createImage(imageCreateInfo);
            m_shadowMomentsImageColors[1]->createImage(imageCreateInfo);
            imageViewCreateInfo = std::make_shared<VkImageViewCreateInfo>();
            m_shadowMomentsImageColors[0]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            m_shadowMomentsImageColors[0]->createImageView(*imageViewCreateInfo);
            m_shadowMomentsImageColors[1]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            m_shadowMomentsImageColors[1]->createImageView(*imageViewCreateInfo);
            m_shadowMomentsImageColors[0]->sampler = m_offscreenSampler->getSampler();
            m_shadowMomentsImageColors[1]->sampler = m_offscreenSampler->getSampler();

            m_worldPosImage.push_back(std::make_shared<Image>(device));
            m_worldPosImage.push_back(std::make_shared<Image>(device));
            imageCreateInfo.format = worldPosColorFormat;
            m_worldPosImage[0]->createImage(imageCreateInfo);
            m_worldPosImage[1]->createImage(imageCreateInfo);
            m_worldPosImage[0]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = worldPosColorFormat;
            m_worldPosImage[0]->createImageView(*imageViewCreateInfo);
            m_worldPosImage[1]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = worldPosColorFormat;
            m_worldPosImage[1]->createImageView(*imageViewCreateInfo);
            m_worldPosImage[0]->sampler = m_offscreenSampler->getSampler();
            m_worldPosImage[1]->sampler = m_offscreenSampler->getSampler();

            m_rayTracingGuideImage.push_back(std::make_shared<Image>(device));
            m_rayTracingGuideImage.push_back(std::make_shared<Image>(device));
            imageCreateInfo.format = worldPosColorFormat;
            m_rayTracingGuideImage[0]->createImage(imageCreateInfo);
            m_rayTracingGuideImage[1]->createImage(imageCreateInfo);
            m_rayTracingGuideImage[0]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = worldPosColorFormat;
            m_rayTracingGuideImage[0]->createImageView(*imageViewCreateInfo);
            m_rayTracingGuideImage[1]->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = worldPosColorFormat;
            m_rayTracingGuideImage[1]->createImageView(*imageViewCreateInfo);
            m_rayTracingGuideImage[0]->sampler = m_offscreenSampler->getSampler();
            m_rayTracingGuideImage[1]->sampler = m_offscreenSampler->getSampler();

            m_denoisingAccumulationImage = std::make_shared<Image>(device);
            imageCreateInfo.format = offscreenColorFormat;
            m_denoisingAccumulationImage->createImage(imageCreateInfo);
            m_denoisingAccumulationImage->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = offscreenColorFormat;
            m_denoisingAccumulationImage->createImageView(*imageViewCreateInfo);
            m_denoisingAccumulationImage->sampler = m_offscreenSampler->getSampler();


            //depth
            offscreenImageDepth = std::make_shared<Image>(device);
            offscreenImageDepth->setDefaultImageCreateInfo(imageCreateInfo);
            imageCreateInfo.format = offscreenDepthFormat;
            imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageCreateInfo.extent = imageExtent;
            offscreenImageDepth->createImage(imageCreateInfo);

            offscreenImageDepth->setDefaultImageViewCreateInfo(*imageViewCreateInfo);
            imageViewCreateInfo->format = imageCreateInfo.format;
            imageViewCreateInfo->subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            offscreenImageDepth->createImageView(*imageViewCreateInfo);
        }

        {
            device.transitionImageLayout(m_offscreenImageColors[0]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_offscreenImageColors[1]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_shadowTermImageColors[0]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_shadowTermImageColors[1]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_shadowMomentsImageColors[0]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_shadowMomentsImageColors[1]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_worldPosImage[0]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_worldPosImage[1]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_rayTracingGuideImage[0]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_rayTracingGuideImage[1]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            
            device.transitionImageLayout(m_denoisingAccumulationImage->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_sceneColorImageColors[0]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(m_sceneColorImageColors[1]->getImage(),
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
            device.transitionImageLayout(offscreenImageDepth->getImage(), VK_IMAGE_LAYOUT_UNDEFINED,
                                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                         {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1});
        }

        {
            if (m_sceneColorRenderPass == VK_NULL_HANDLE) {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format = offscreenColorFormat;
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkAttachmentDescription depthAttachment{};
            depthAttachment.format = offscreenDepthFormat;
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorAttachmentRef{};
            colorAttachmentRef.attachment = 0;
            colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthAttachmentRef{};
            depthAttachmentRef.attachment = 1;
            depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorAttachmentRef;
            subpass.pDepthStencilAttachment = &depthAttachmentRef;

            std::array<VkAttachmentDescription, 2> attachments{colorAttachment, depthAttachment};
            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            renderPassInfo.pAttachments = attachments.data();
            renderPassInfo.subpassCount = 1;
            renderPassInfo.pSubpasses = &subpass;

            if (vkCreateRenderPass(device.device(), &renderPassInfo, nullptr, &m_sceneColorRenderPass) != VK_SUCCESS) {
                throw std::runtime_error("failed to create scene color render pass");
            }
            }

            m_sceneColorFramebuffers.resize(m_sceneColorImageColors.size(), VK_NULL_HANDLE);
            for (size_t i = 0; i < m_sceneColorImageColors.size(); ++i) {
                VkImageView attachmentsForFramebuffer[2] = {
                    *m_sceneColorImageColors[i]->getImageView(),
                    *offscreenImageDepth->getImageView()
                };

                VkFramebufferCreateInfo framebufferCreateInfo{};
                framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferCreateInfo.renderPass = m_sceneColorRenderPass;
                framebufferCreateInfo.attachmentCount = 2;
                framebufferCreateInfo.pAttachments = attachmentsForFramebuffer;
                framebufferCreateInfo.width = m_sceneRenderExtent.width;
                framebufferCreateInfo.height = m_sceneRenderExtent.height;
                framebufferCreateInfo.layers = 1;

                if (vkCreateFramebuffer(device.device(), &framebufferCreateInfo, nullptr, &m_sceneColorFramebuffers[i]) != VK_SUCCESS) {
                    throw std::runtime_error("failed to create scene color framebuffer");
                }
            }
        }
    }

    const std::shared_ptr<Image> &Renderer::getShadowImage() const {
        return shadowImage;
    }

    const std::shared_ptr<Sampler> &Renderer::getShadowSampler() const {
        return shadowSampler;
    }

    void Renderer::setShadowMapSynchronization(VkCommandBuffer commandBuffer) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = getShadowImage()->getImage();

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        barrier.subresourceRange = subresourceRange;

        VkPipelineStageFlagBits srcStage, dstStage;

        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        srcStage = static_cast<VkPipelineStageFlagBits>(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;


        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

    }

#ifdef RAY_TRACING

    void Renderer::setSceneColorToPostSynchronization(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_sceneColorImageColors[imageIndex]->getImage();

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        barrier.subresourceRange = subresourceRange;

        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);
    }
    void Renderer::setDenoiseComputeToPostSynchronization(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_offscreenImageColors[imageIndex]->getImage();

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        barrier.subresourceRange = subresourceRange;

        VkPipelineStageFlagBits srcStage, dstStage;

        srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;


        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

        barrier.image = m_denoisingAccumulationImage->getImage();
        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);
    }

    void Renderer::setDenoiseRtxToComputeSynchronization(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        barrier.subresourceRange = subresourceRange;

        VkPipelineStageFlagBits srcStage, dstStage;

        srcStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

        barrier.image = m_offscreenImageColors[imageIndex]->getImage();
        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

        barrier.image = m_worldPosImage[imageIndex]->getImage();
        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

        barrier.image = m_rayTracingGuideImage[imageIndex]->getImage();
        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

        barrier.image = m_shadowTermImageColors[imageIndex]->getImage();
        vkCmdPipelineBarrier(commandBuffer,
                             srcStage, dstStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

    }

#endif


}






