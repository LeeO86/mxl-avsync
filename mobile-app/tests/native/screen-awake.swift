import Foundation
@main struct ScreenAwakeTests {
 static func main() {
  var timer=false,writes=[Bool]()
  let state=ScreenAwakeState(foreground:true,read:{timer},write:{timer=$0;writes.append($0)})
  state.setActive(true,owner:"capture");precondition(timer)
  state.setActive(true,owner:"generator");state.setActive(false,owner:"capture");precondition(timer)
  state.setActive(false,owner:"capture");precondition(timer) // Repeated cleanup must not release generator.
  state.setForeground(false);precondition(!timer)
  state.setForeground(true);precondition(timer)
  state.setActive(false,owner:"generator");precondition(!timer)
  let count=writes.count;state.setForeground(false);state.setForeground(true);precondition(writes.count==count)
  state.setForeground(false);state.setActive(true,owner:"review");precondition(!timer)
  state.setForeground(true);precondition(timer)
  state.setForeground(false);state.setActive(false,owner:"review");state.setForeground(true);precondition(!timer)
  timer=true;state.setActive(true,owner:"capture");state.setActive(false,owner:"capture");precondition(timer) // Preserve a pre-existing owner outside this controller.
  print("PASS: screen awake owners, duplicate cleanup, pause/background/resume, inactive acquire/release, baseline restoration")
 }
}
