#include "property.h"
#include "editor\editor.h"
#include "editor\widgets\worldEditor.h"
#include "imgui-docking\imgui.h"

namespace VulkanTest
{
namespace Editor
{
	PropertyWidget::PropertyWidget(EditorApp& editor_) :
		editor(editor_),
		worldEditor(editor_.GetWorldEditor())
	{
		componentFilter[0] = '\0';
	}

	PropertyWidget::~PropertyWidget()
	{
	}

	void PropertyWidget::Update(F32 dt)
	{
	}

	void PropertyWidget::OnGUI()
	{
		if (!isOpen) return;

		auto& entities = worldEditor.GetSelectedEntities();
		if (entities.empty())
			return;

		if (entities.size() > 1)
		{
			if (ImGui::Begin(ICON_FA_INFO_CIRCLE "Inspector##inspector", &isOpen))
			{
				ImGuiEx::Label("ID");
				ImGui::Text("%s", "Multiple objects");
				ImGuiEx::Label("Name");
				ImGui::Text("%s", "Multi-object editing not supported.");
			}
			ImGui::End();
			return;
		}

		ECS::EntityID entity = entities[0];
		if (ImGui::Begin("Inspector##inspector", &isOpen))
		{
			ShowBaseProperties(entity);

			ImGui::Separator();
			const float x = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Add component").x - ImGui::GetStyle().FramePadding.x * 2) * 0.5f;
			ImGui::SetCursorPosX(x);
			if (ImGui::Button(ICON_FA_PLUS "Add component"))
				ImGui::OpenPopup("AddComponentPopup");

			if (ImGui::BeginPopup("AddComponentPopup", ImGuiWindowFlags_AlwaysAutoResize)) 
			{
				const float w = ImGui::GetStyle().ItemSpacing.x * 2;
				ImGui::SetNextItemWidth(-w);
				ImGui::SetNextItemWidth(200);
				ImGui::InputTextWithHint("##filter", "Filter", componentFilter, sizeof(componentFilter));
				ImGui::SameLine();
				if (ImGui::Button("Clear filter")) {
					componentFilter[0] = '\0';
				}

				ImGui::EndPopup();
			}
		}
		ImGui::End();
	}

	const char* PropertyWidget::GetName()
	{
		return "PropertyWidget";
	}

	void PropertyWidget::ShowBaseProperties(ECS::EntityID entity)
	{
		auto world = worldEditor.GetWorld();
		const char* tmp = world->GetEntityName(entity);

		char name[32];
		CopyString(name, tmp);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::InputTextWithHint("##name", "Name", name, sizeof(name)))
			world->SetEntityName(entity, name);

		if (!ImGui::TreeNodeEx("General", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		ImGuiEx::Label("ID");
		ImGui::Text("%d", entity);
		ECS::EntityID parent = world->GetEntityParent(entity);
		if (parent != ECS::INVALID_ENTITY)
		{
			const char* tmp = world->GetEntityName(entity);
			ImGuiEx::Label("Parent");
			ImGui::Text("%s", tmp);
		}

		TransformComponent* transformComp = world->GetComponent<TransformComponent>(entity);
		if (transformComp != nullptr)
		{
			auto& transform = transformComp->transform;
			ImGuiEx::Label("Position");
			if (ImGui::DragScalarN("##pos", ImGuiDataType_Float, transform.translation.data, 3, 1.f, 0, 0, "%.3f"))
				transform.isDirty = true;

			ImGuiEx::Label("Rotation");
			if (ImGui::DragScalarN("##rotation", ImGuiDataType_Float, transform.rotation.data, 4, 1.f, 0, 0, "%.3f"))
				transform.isDirty = true;

			F32 scale = transform.scale.x;
			ImGuiEx::Label("Scale");
			if (ImGui::DragFloat("##scale", &scale, 0.1f, 0, FLT_MAX))
			{
				transform.scale.y = transform.scale.x;
				transform.scale.z = transform.scale.x;
				transform.isDirty = true;
			}		
		}

		ImGui::TreePop();
	}
}
}
