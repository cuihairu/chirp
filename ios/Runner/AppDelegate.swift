import UIKit
import Flutter
import CallKit
import PushKit
import AVFoundation

@UIApplicationMain
@objc class AppDelegate: FlutterAppDelegate {
    var callKitProvider: CXProvider?
    var callController: CXCallController?
    var currentCallUUID: UUID?
    var flutterMethodChannel: FlutterMethodChannel?

    override func application(
        _ application: UIApplication,
        didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
    ) -> Bool {
        let controller = window?.rootViewController as! FlutterViewController
        flutterMethodChannel = FlutterMethodChannel(
            name: "com.chirp.mobile/native",
            binaryMessenger: controller.binaryMessenger
        )

        flutterMethodChannel?.setMethodCallHandler { [weak self] (call, result) in
            guard let self = self else { return }
            self.handleMethodCall(call, result: result)
        }

        setupCallKit()
        registerForPushNotifications()

        return super.application(application, didFinishLaunchingWithOptions: launchOptions)
    }

    // MARK: - Flutter Method Channel Handler

    private func handleMethodCall(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        switch call.method {
        case "getPlatformVersion":
            result("iOS \(UIDevice.current.systemVersion)")
        case "getDeviceId":
            result(getDeviceId())
        case "startCall":
            if let args = call.arguments as? [String: Any],
               let roomId = args["roomId"] as? String,
               let roomName = args["roomName"] as? String {
                startCall(roomId: roomId, roomName: roomName)
                result(true)
            } else {
                result(FlutterError(code: "INVALID_ARGS", message: "Missing roomId or roomName", details: nil))
            }
        case "endCall":
            endCall()
            result(true)
        case "setMuted":
            if let args = call.arguments as? [String: Any],
               let isMuted = args["isMuted"] as? Bool {
                setMuted(isMuted)
                result(true)
            } else {
                result(false)
            }
        case "setSpeakerOn":
            if let args = call.arguments as? [String: Any],
               let isOn = args["isOn"] as? Bool {
                setSpeakerOn(isOn)
                result(true)
            } else {
                result(false)
            }
        default:
            result(FlutterMethodNotImplemented)
        }
    }

    // MARK: - Device ID

    private func getDeviceId() -> String {
        return UIDevice.current.identifierForVendor?.uuidString ?? "unknown"
    }

    // MARK: - CallKit Setup

    private func setupCallKit() {
        let configuration = CXProviderConfiguration(localizedName: "Chirp")
        configuration.supportsVideo = false
        configuration.maximumCallGroups = 1
        configuration.maximumCallsPerCallGroup = 1
        configuration.supportedHandleTypes = [.generic]
        configuration.iconTemplateImageData = UIImage(named: "AppIcon")?.pngData()

        callKitProvider = CXProvider(configuration: configuration)
        callController = CXCallController()

        callKitProvider?.setDelegate(self, queue: nil)
    }

    // MARK: - Call Control

    func startCall(roomId: String, roomName: String) {
        let uuid = UUID()
        currentCallUUID = uuid

        let handle = CXHandle(type: .generic, value: roomName)
        let startCallAction = CXStartCallAction(call: uuid, handle: handle)

        let transaction = CXTransaction(action: startCallAction)

        callController?.request(transaction) { error in
            if let error = error {
                print("Error starting call: \(error.localizedDescription)")
            }
        }
    }

    func endCall() {
        guard let uuid = currentCallUUID else { return }

        let endCallAction = CXEndCallAction(call: uuid)
        let transaction = CXTransaction(action: endCallAction)

        callController?.request(transaction) { error in
            if let error = error {
                print("Error ending call: \(error.localizedDescription)")
            }
        }

        currentCallUUID = nil
    }

    func reportIncomingCall(roomId: String, callerName: String) {
        let uuid = UUID()
        currentCallUUID = uuid

        let handle = CXHandle(type: .generic, value: callerName)
        let callUpdate = CXCallUpdate()
        callUpdate.remoteHandle = handle
        callUpdate.hasVideo = false
        callUpdate.localizedCallerName = callerName

        callKitProvider?.reportNewIncomingCall(with: uuid, update: callUpdate) { error in
            if let error = error {
                print("Error reporting incoming call: \(error.localizedDescription)")
            }
        }
    }

    func setMuted(_ isMuted: Bool) {
        guard let uuid = currentCallUUID else { return }

        let muteAction = CXSetMutedCallAction(call: uuid, muted: isMuted)
        let transaction = CXTransaction(action: muteAction)

        callController?.request(transaction) { error in
            if let error = error {
                print("Error setting muted: \(error.localizedDescription)")
            }
        }
    }

    func setSpeakerOn(_ isOn: Bool) {
        do {
            let audioSession = AVAudioSession.sharedInstance()
            try audioSession.setCategory(isOn ? .playAndRecord : .playback, mode: .voiceChat, options: .defaultToSpeaker)
            try audioSession.setActive(true)
        } catch {
            print("Error setting speaker: \(error.localizedDescription)")
        }
    }

    // MARK: - Push Notifications

    private func registerForPushNotifications() {
        UNUserNotificationCenter.current().delegate = self

        let authOptions: UNAuthorizationOptions = [.alert, .sound, .badge]
        UNUserNotificationCenter.current().requestAuthorization(
            options: authOptions
        ) { granted, error in
            if granted {
                DispatchQueue.main.async {
                    UIApplication.shared.registerForRemoteNotifications()
                }
            }
        }

        // Register for VoIP push notifications
        let voipRegistry = PKPushRegistry(queue: DispatchQueue.main)
        voipRegistry.delegate = self
        voipRegistry.desiredPushTypes = [.voIP]
    }

    override func application(_ application: UIApplication, didRegisterForRemoteNotificationsWithDeviceToken deviceToken: Data) {
        let tokenParts = deviceToken.map { data in String(format: "%02.2hhx", data) }
        let token = tokenParts.joined()

        // Send token to Flutter
        flutterMethodChannel?.invokeMethod("onFCMToken", arguments: token)

        // Store token
        UserDefaults.standard.set(token, forKey: "fcm_token")
    }

    override func application(_ application: UIApplication, didFailToRegisterForRemoteNotificationsWithError error: Error) {
        print("Failed to register for remote notifications: \(error.localizedDescription)")
    }

    override func userNotificationCenter(_ center: UNUserNotificationCenter, didReceive response: UNNotificationResponse, withCompletionHandler completionHandler: @escaping () -> Void) {
        let userInfo = response.notification.request.content.userInfo

        // Handle notification tap
        if let roomId = userInfo["room_id"] as? String {
            flutterMethodChannel?.invokeMethod("onNotificationTapped", arguments: ["room_id": roomId])
        }

        completionHandler()
    }
}

// MARK: - CXProviderDelegate

extension AppDelegate: CXProviderDelegate {
    func providerDidReset(_ provider: CXProvider) {
        // Handle provider reset
        endCall()
    }

    func provider(_ provider: CXProvider, perform action: CXStartCallAction) {
        // Notify Flutter that call is starting
        flutterMethodChannel?.invokeMethod("onCallStarted", arguments: [
            "roomId": action.callUUID.uuidString,
            "handle": action.handle.value
        ])
        action.fulfill()
    }

    func provider(_ provider: CXProvider, perform action: CXAnswerCallAction) {
        // Notify Flutter that call was answered
        flutterMethodChannel?.invokeMethod("onCallAnswered", arguments: [
            "roomId": action.callUUID.uuidString
        ])
        action.fulfill()
    }

    func provider(_ provider: CXProvider, perform action: CXEndCallAction) {
        // Notify Flutter that call ended
        flutterMethodChannel?.invokeMethod("onCallEnded", arguments: [
            "roomId": action.callUUID.uuidString
        ])
        action.fulfill()
        currentCallUUID = nil
    }

    func provider(_ provider: CXProvider, perform action: CXSetMutedCallAction) {
        // Notify Flutter of mute state change
        flutterMethodChannel?.invokeMethod("onMuteChanged", arguments: [
            "isMuted": action.isMuted
        ])
        action.fulfill()
    }

    func provider(_ provider: CXProvider, timedOutPerforming action: CXAction) {
        action.fail()
    }
}

// MARK: - PKPushRegistryDelegate

extension AppDelegate: PKPushRegistryDelegate {
    func pushRegistry(_ registry: PKPushRegistry, didUpdate credentials: PKPushCredentials, for type: PKPushType) {
        let tokenParts = credentials.token.map { data in String(format: "%02.2hhx", data) }
        let token = tokenParts.joined()

        // Send VoIP token to Flutter
        flutterMethodChannel?.invokeMethod("onVoIPToken", arguments: token)
    }

    func pushRegistry(_ registry: PKPushRegistry, didReceiveIncomingPushWith payload: PKPushPayload, for type: PKPushType) {
        let payloadDict = payload.dictionaryPayload

        if type == .voIP {
            // Handle incoming VoIP push (incoming call)
            if let callerName = payloadDict["caller_name"] as? String,
               let roomId = payloadDict["room_id"] as? String {
                reportIncomingCall(roomId: roomId, callerName: callerName)
            }
        }
    }

    func pushRegistry(_ registry: PKPushRegistry, didInvalidatePushTokenFor type: PKPushType) {
        print("Push token invalidated for type: \(type)")
    }
}
