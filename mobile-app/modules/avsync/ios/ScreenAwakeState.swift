import Foundation

// Main-thread policy, independent of UIKit so owner/lifecycle behavior is testable.
final class ScreenAwakeState {
  private var owners = Set<String>()
  private var original: Bool?
  private var foreground: Bool
  private let read: () -> Bool
  private let write: (Bool) -> Void
  init(foreground: Bool, read: @escaping () -> Bool, write: @escaping (Bool) -> Void) {
    self.foreground=foreground;self.read=read;self.write=write
  }
  func setActive(_ active: Bool, owner: String) {
    if active {
      if owners.isEmpty { original=read() }
      owners.insert(owner)
    } else {
      guard owners.remove(owner) != nil else { return }
    }
    apply()
    if owners.isEmpty { original=nil }
  }
  func setForeground(_ active: Bool) {
    guard active != foreground else { return }
    foreground=active;apply()
  }
  private func apply() {
    guard let original else { return }
    write(foreground && !owners.isEmpty ? true : original)
  }
}
