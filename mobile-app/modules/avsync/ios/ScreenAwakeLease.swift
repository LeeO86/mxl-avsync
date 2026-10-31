import UIKit

// Every owner releases only its own request; all UIKit access stays on main.
final class ScreenAwakeLease {
  private let owner=UUID().uuidString
  func setActive(_ active: Bool) { Self.update(owner,active) }
  deinit { Self.update(owner,false) }
  private static func update(_ owner: String, _ active: Bool) {
    if Thread.isMainThread { ScreenAwakeCoordinator.shared.state.setActive(active,owner:owner) }
    else { DispatchQueue.main.async { ScreenAwakeCoordinator.shared.state.setActive(active,owner:owner) } }
  }
}
private final class ScreenAwakeCoordinator {
  static let shared=ScreenAwakeCoordinator()
  let state: ScreenAwakeState
  private var notifications=[NSObjectProtocol]()
  private init() {
    state=ScreenAwakeState(foreground:UIApplication.shared.applicationState == .active,
      read:{ UIApplication.shared.isIdleTimerDisabled },write:{ UIApplication.shared.isIdleTimerDisabled=$0 })
    notifications.append(NotificationCenter.default.addObserver(forName:UIApplication.willResignActiveNotification,object:nil,queue:.main) { [weak self] _ in self?.state.setForeground(false) })
    notifications.append(NotificationCenter.default.addObserver(forName:UIApplication.didBecomeActiveNotification,object:nil,queue:.main) { [weak self] _ in self?.state.setForeground(true) })
  }
}
