import UIKit
struct Card<T: Equatable> { let values: [T]; func contains(_ value: T) -> Bool { values.contains(value) } }
final class SwiftDelegate: UIResponder, UIApplicationDelegate {
    var window: UIWindow?
    let label = UILabel(frame: CGRect(x: 20,y: 90,width: 280,height: 50))
    var count = 0
    func application(_ application: UIApplication, didFinishLaunchingWithOptions options: [UIApplication.LaunchOptionsKey:Any]?) -> Bool {
        let window = UIWindow(frame: UIScreen.main.bounds)
        let controller = UIViewController(); window.rootViewController = controller
        controller.view.backgroundColor = .white
        label.textColor = .black; label.text = "SwiftCards: 0"; controller.view.addSubview(label)
        let button = UIButton(type: .system); button.frame = CGRect(x: 20,y: 180,width: 280,height: 50)
        button.setTitle("Deal", for: .normal); button.addTarget(self,action: #selector(deal),for: .touchUpInside)
        controller.view.addSubview(button); self.window = window; window.makeKeyAndVisible(); return true
    }
    @objc func deal() {
        let card = Card(values:[2,3,5]); let transform: (Int)->Int = { $0 * 2 }
        if card.contains(3) { count += transform(3) }
        label.text = "SwiftCards: \(count)"
    }
}
UIApplicationMain(CommandLine.argc,CommandLine.unsafeArgv,nil,NSStringFromClass(SwiftDelegate.self))
